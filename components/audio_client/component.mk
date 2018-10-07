
COMPONENT_SRCDIRS := src

$(call compile_only_if,$(ENABLE_SIP_AUDIO_CODEC_G711),g711.o)
