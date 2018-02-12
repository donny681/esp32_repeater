deps_config := \
	/home/ai-thinker/esp/esp-idf/components/app_trace/Kconfig \
	/home/ai-thinker/esp/esp-idf/components/aws_iot/Kconfig \
	/home/ai-thinker/esp/esp-idf/components/bt/Kconfig \
	/home/ai-thinker/esp/esp-idf/components/esp32/Kconfig \
	/home/ai-thinker/esp/esp-idf/components/ethernet/Kconfig \
	/home/ai-thinker/esp/esp-idf/components/fatfs/Kconfig \
	/home/ai-thinker/esp/esp-idf/components/freertos/Kconfig \
	/home/ai-thinker/esp/esp-idf/components/heap/Kconfig \
	/home/ai-thinker/esp/esp-idf/components/libsodium/Kconfig \
	/home/ai-thinker/esp/esp-idf/components/log/Kconfig \
	/home/ai-thinker/esp/esp-idf/components/lwip/Kconfig \
	/home/ai-thinker/esp/esp-idf/components/mbedtls/Kconfig \
	/home/ai-thinker/esp/esp-idf/components/openssl/Kconfig \
	/home/ai-thinker/esp/esp-idf/components/pthread/Kconfig \
	/home/ai-thinker/esp/esp-idf/components/spi_flash/Kconfig \
	/home/ai-thinker/esp/esp-idf/components/spiffs/Kconfig \
	/home/ai-thinker/esp/esp-idf/components/tcpip_adapter/Kconfig \
	/home/ai-thinker/esp/esp-idf/components/wear_levelling/Kconfig \
	/home/ai-thinker/esp/esp-idf/components/bootloader/Kconfig.projbuild \
	/home/ai-thinker/esp/esp-idf/components/esptool_py/Kconfig.projbuild \
	/home/ai-thinker/esp/esp-idf/components/partition_table/Kconfig.projbuild \
	/home/ai-thinker/esp/esp-idf/Kconfig

include/config/auto.conf: \
	$(deps_config)


$(deps_config): ;
