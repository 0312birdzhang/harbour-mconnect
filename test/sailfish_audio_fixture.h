#pragma once

#include <QString>
#include <QList>
#include <cstdint>

struct NativeAudioFixture {
  int opens = 0, plays = 0, destroys = 0, status = 0, state = 0;
  int64_t position = 0, duration = 100000000;
  QString uri;
  double rate = 1;
  bool seekSucceeds = true;
  QList<double> volumes, bands;
};

NativeAudioFixture& NativeTestAudio();
