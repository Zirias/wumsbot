wumsbot_MODULES:= main infodb
wumsbot_LDFLAGS:= -pthread
wumsbot_PKGDEPS:= ircbot >= 1.0
$(call binrules, wumsbot)
