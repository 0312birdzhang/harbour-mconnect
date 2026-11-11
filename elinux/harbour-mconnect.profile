# Additional service names used by the native process and its D-Bus clients.
dbus-user.own org.sailfishos.Mconnect.Core
dbus-user.own org.sailfishos.Mconnect.Silica

# Directed SDK updates to the caller's temporary client name.
dbus-user.call org.sailplay.Client.*=org.sailplay.Session1.StateChanged@/org/sailplay/v1/*
dbus-user.call org.sailplay.Client.*=org.sailplay.Session1.RequestFinished@/org/sailplay/v1/*
dbus-user.call org.sailplay.Client.*=org.sailplay.Navigation1.GuidanceChanged@/org/sailplay/v1/*
dbus-user.call org.sailplay.Client.*=org.sailplay.Content1.ContentChanged@/org/sailplay/v1/*
dbus-user.call org.sailplay.Client.*=org.freedesktop.DBus.Properties.PropertiesChanged@/org/sailplay/v1/*

# Silica reads the active profile and its feedback settings.
dbus-user.call com.nokia.profiled=com.nokia.profiled.get_profile@/com/nokia/profiled
dbus-user.call com.nokia.profiled=com.nokia.profiled.get_profiles@/com/nokia/profiled
dbus-user.call com.nokia.profiled=com.nokia.profiled.get_value@/com/nokia/profiled
dbus-user.broadcast com.nokia.profiled=com.nokia.profiled.profile_changed@/com/nokia/profiled

# Keep the existing storage locations available on upgrades and fresh installs.
mkdir ${HOME}/.local/share/harbour-mconnect
whitelist ${HOME}/.local/share/harbour-mconnect
mkdir ${HOME}/.cache/harbour-mconnect
whitelist ${HOME}/.cache/harbour-mconnect
