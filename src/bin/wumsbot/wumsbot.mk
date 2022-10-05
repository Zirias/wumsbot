wumsbot_MODULES:= main
wumsbot_LDFLAGS:= -pthread
wumsbot_PKGDEPS:= ircbot >= 0.1
$(call binrules, wumsbot)
