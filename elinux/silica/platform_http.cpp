#include "platform_http.h"
#include <QElapsedTimer>
#include <QNetworkReply>
#include <QTimer>

QUrl NativePlatformUrl(const QUrl& base,const QList<QPair<QString,QString>>& query) {
  QByteArray parameters;
  for(const auto& pair:query) {
    if(!parameters.isEmpty())parameters+='&';
    parameters+=QUrl::toPercentEncoding(pair.first)+'='+QUrl::toPercentEncoding(pair.second);
  }
  // Percent-encode literal '+' as well: form-style API parsers otherwise turn
  // an artist/song query such as "A+B" into "A B".
  return QUrl::fromEncoded(base.toEncoded(QUrl::RemoveQuery|QUrl::RemoveFragment)+'?'+parameters);
}

struct NativePlatformHttp::Pending {
  QNetworkRequest request;
  QByteArray method, body;
  Done done;
  QElapsedTimer elapsed;
  int timeout=18000, maximum=4*1024*1024, redirects=0, retries=0;
  bool safeGet=true;
  bool publicKugou=false;
  bool followRedirects=true;
  bool legacyKugouAccount=false;
};
NativePlatformHttp::NativePlatformHttp(QObject* parent):QObject(parent) {}
NativePlatformHttp::~NativePlatformHttp() {
  for(auto* reply:network_.findChildren<QNetworkReply*>()){reply->disconnect(this);reply->abort();}
}
void NativePlatformHttp::request(const QNetworkRequest& request,const QByteArray& method,const QByteArray& body,Done done,int timeoutMs,int maxBytes,bool safeGet,bool publicKugou,bool followRedirects,bool legacyKugouAccount) {
  auto pending=std::make_shared<Pending>();pending->request=request;pending->method=method;pending->body=body;pending->done=std::move(done);
  pending->timeout=timeoutMs;pending->maximum=maxBytes;pending->elapsed.start();
  pending->safeGet=safeGet;
  pending->publicKugou=publicKugou;
  pending->followRedirects=followRedirects;
  pending->legacyKugouAccount=legacyKugouAccount;
  // Defer even validation errors: a caller must be able to install its callback.
  QTimer::singleShot(0,this,[this,pending](){send(pending);});
}
void NativePlatformHttp::send(std::shared_ptr<Pending> p) {
  const auto url=p->request.url();
  const bool local=url.scheme()=="http" && (url.host()=="127.0.0.1" || url.host()=="localhost");
  // Legacy public Kugou catalog hosts lack usable TLS. Only these fixed GET
  // paths may opt in; never send account credentials or permit arbitrary HTTP.
  const bool kugou=p->publicKugou && url.scheme()=="http" && url.port(80)==80 && p->method=="GET" && p->body.isEmpty() &&
    !p->request.hasRawHeader("Cookie") && !p->request.hasRawHeader("Authorization") &&
    ((url.host()=="mobilecdn.kugou.com" && QStringList{"/api/v3/search/song","/api/v3/rank/list","/api/v3/rank/song","/api/v3/singer/album","/api/v3/album/info","/api/v3/album/song","/api/v3/recommend/song"}.contains(url.path())) ||
     (url.host()=="mobilecdnbj.kugou.com" && QStringList{"/api/v3/singer/info","/api/v3/singer/song","/api/v5/special/detail","/api/v5/special/song"}.contains(url.path())));
  // These existing CDN account operations use query credentials. The opt-in
  // is separate from public catalog requests and restricted to their paths.
  const bool kugouAccount=p->legacyKugouAccount && url.scheme()=="http" && url.port(80)==80 &&
    url.host()=="mobilecdn.kugou.com" && p->method=="GET" && p->body.isEmpty() &&
    !p->request.hasRawHeader("Cookie") && !p->request.hasRawHeader("Authorization") &&
    QStringList{"/api/v2/user/vip","/api/v5/song/collect","/api/v5/song/uncollect"}.contains(url.path());
  const int remaining=p->timeout-int(p->elapsed.elapsed());
  const auto fail=[p](const QString& error){NativeHttpResult result;result.error=error;p->done(result);};
  if(remaining<=0){fail("timeout");return;}
  if((url.scheme()!="https" && !local && !kugou && !kugouAccount) || !url.userInfo().isEmpty() || (p->method!="GET" && p->method!="POST") || p->maximum<1 || p->maximum>8*1024*1024 || p->body.size()>1024*1024){fail("invalid_request");return;}
  // Every request supplies its authoritative account cookie. Never retain a
  // previous account's Qt cookie jar or forward it through a redirect.
  p->request.setAttribute(QNetworkRequest::CookieLoadControlAttribute,QNetworkRequest::Manual);
  p->request.setAttribute(QNetworkRequest::CookieSaveControlAttribute,QNetworkRequest::Manual);
#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
  p->request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
#endif
  auto* reply=p->method=="GET"?network_.get(p->request):network_.post(p->request,p->body);
  auto bytes=std::make_shared<QByteArray>();auto tooLarge=std::make_shared<bool>(false);
  QTimer::singleShot(remaining,reply,&QNetworkReply::abort);
  connect(reply,&QNetworkReply::metaDataChanged,this,[reply,p,tooLarge](){if(reply->header(QNetworkRequest::ContentLengthHeader).toLongLong()>p->maximum){*tooLarge=true;reply->abort();}});
  connect(reply,&QNetworkReply::readyRead,this,[reply,p,bytes,tooLarge](){
    if(reply->bytesAvailable()>p->maximum-bytes->size()){*tooLarge=true;reply->abort();return;}
    bytes->append(reply->readAll());
  });
  connect(reply,&QNetworkReply::finished,this,[this,p,reply,bytes,tooLarge,fail](){
    reply->deleteLater();
    if(*tooLarge || reply->bytesAvailable()>p->maximum-bytes->size()){fail("response_too_large");return;}
    bytes->append(reply->readAll());
    NativeHttpResult result;result.status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();result.headers=reply->rawHeaderPairs();
    const auto target=reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if(!p->followRedirects && result.status>=300 && result.status<400) {
      result.body=*bytes;
      p->done(result);
      return;
    }
    if(!target.isEmpty()) {
      const auto from=p->request.url(),next=from.resolved(target);
      if(p->method!="GET" || !p->safeGet || ++p->redirects>3 || next.scheme()!=from.scheme() || next.host()!=from.host() || next.port(next.scheme()=="https"?443:80)!=from.port(from.scheme()=="https"?443:80)) {fail("redirect_rejected");return;}
      p->request.setUrl(next);send(p);return;
    }
    const bool timeout=p->elapsed.elapsed()>=p->timeout || reply->error()==QNetworkReply::OperationCanceledError;
    const bool transient=result.status==429 || result.status==502 || result.status==503 || result.status==504 || reply->error()==QNetworkReply::RemoteHostClosedError || reply->error()==QNetworkReply::TemporaryNetworkFailureError;
    if(!timeout && transient && p->safeGet && p->method=="GET" && p->retries++==0 && p->elapsed.elapsed()+200<p->timeout){QTimer::singleShot(200,this,[this,p](){send(p);});return;}
    if(timeout)result.error="timeout";
    else if(result.status==401 || result.status==403)result.error="login_required";
    else if(reply->error()!=QNetworkReply::NoError || result.status!=200)result.error="network_error";
    else result.body=*bytes;
    p->done(result);
  });
}
