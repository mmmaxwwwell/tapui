cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tapui)

target_sources(app PRIVATE
  src/main.c
  src/tapui/proto.c
  src/tapui/ndef.c
  src/tapui/tapui_ble.c
)
target_sources_ifdef(CONFIG_TAPUI_NFC app PRIVATE src/tapui/nfc_tag.c)
