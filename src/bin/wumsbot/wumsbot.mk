wumsbot_MODULES:= main infodb
wumsbot_LDFLAGS:= -pthread
wumsbot_PKGDEPS:= ircbot >= 0.1
$(call binrules, wumsbot)
