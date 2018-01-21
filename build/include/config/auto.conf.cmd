deps_config := \
	/home/sky/esp/esp-idf/components/app_trace/Kconfig \
	/home/sky/esp/esp-idf/components/aws_iot/Kconfig \
	/home/sky/esp/esp-idf/components/bt/Kconfig \
	/home/sky/esp/esp-idf/components/esp32/Kconfig \
	/home/sky/esp/esp-idf/components/ethernet/Kconfig \
	/home/sky/esp/esp-idf/components/fatfs/Kconfig \
	/home/sky/esp/esp-idf/components/freertos/Kconfig \
	/home/sky/esp/esp-idf/components/heap/Kconfig \
	/home/sky/esp/esp-idf/components/libsodium/Kconfig \
	/home/sky/esp/esp-idf/components/log/Kconfig \
	/home/sky/esp/esp-idf/components/lwip/Kconfig \
	/home/sky/esp/esp-idf/components/mbedtls/Kconfig \
	/home/sky/esp/esp-idf/components/openssl/Kconfig \
	/home/sky/esp/esp-idf/components/pthread/Kconfig \
	/home/sky/esp/esp-idf/components/spi_flash/Kconfig \
	/home/sky/esp/esp-idf/components/spiffs/Kconfig \
	/home/sky/esp/esp-idf/components/tcpip_adapter/Kconfig \
	/home/sky/esp/esp-idf/components/wear_levelling/Kconfig \
	/home/sky/esp/esp-idf/components/bootloader/Kconfig.projbuild \
	/home/sky/esp/esp-idf/components/esptool_py/Kconfig.projbuild \
	/home/sky/esp/esp-idf/components/partition_table/Kconfig.projbuild \
	/home/sky/esp/esp-idf/Kconfig

include/config/auto.conf: \
	$(deps_config)


$(deps_config): ;
