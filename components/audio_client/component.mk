
COMPONENT_SRCDIRS := src

$(call compile_only_if,$(CONFIG_ENABLE_SIP_AUDIO_CODEC_G711),g711.o)
$(call compile_only_if,$(CONFIG_ENABLE_SIP_AUDIO_CLIENT),audio_client.o)
