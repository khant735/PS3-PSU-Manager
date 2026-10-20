#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir" tests/filen_crypto_test tests/filen_download_test' EXIT
inc=(-Iinclude)
if [[ -n "${FILEN_MBEDTLS_INCLUDE:-}" ]]; then inc+=("-I$FILEN_MBEDTLS_INCLUDE"); fi
crypto=${FILEN_MBEDCRYPTO_LIB:--lmbedcrypto}
g++ -Iinclude tests/test_ca_bundle_path.cpp source/ca_bundle_path.cpp -o "$test_dir/ca-bundle-path"
"$test_dir/ca-bundle-path"
g++ "${inc[@]}" tests/test_filen.cpp source/filen_crypto.cpp "$crypto" -o tests/filen_crypto_test
python3 tests/test_filen_crypto.py
g++ "${inc[@]}" tests/test_filen_json.cpp -o "$test_dir/json"
"$test_dir/json"
g++ "${inc[@]}" tests/test_filen_download.cpp source/filen_download.cpp source/filen_crypto.cpp "$crypto" -lcurl -o tests/filen_download_test
python3 tests/test_filen_download.py
g++ -Iinclude tests/test_ps2wiki_parser.cpp source/ps2wiki_catalogue.cpp -o "$test_dir/catalogue"
"$test_dir/catalogue"
g++ -Iinclude tests/test_ps2homebrew_parser.cpp source/ps2homebrew_catalogue.cpp -o "$test_dir/ps2hb-parser"
"$test_dir/ps2hb-parser"
g++ -Iinclude tests/test_ps2homebrew_payload.cpp source/ps2homebrew_catalogue.cpp -o "$test_dir/ps2hb-payload"
"$test_dir/ps2hb-payload"
g++ -std=c++17 -Iinclude tests/test_ui_text_layout.cpp source/ui_text_layout.cpp -o "$test_dir/ui-text-layout"
"$test_dir/ui-text-layout"
g++ -std=c++17 -Iinclude tests/test_catalogue_action.cpp source/catalogue_action.cpp -o "$test_dir/catalogue-action"
"$test_dir/catalogue-action"
g++ -std=c++17 -Iinclude tests/test_icn_texture.cpp source/icn_texture.cpp -o "$test_dir/icn-texture"
"$test_dir/icn-texture"
python3 tests/test_catalogue_layout.py
python3 tests/test_homebrew_catalogue_layout.py
g++ -std=c++17 -Wall -Wextra -Iinclude tests/zip_archive_test_driver.cpp source/zip_extract.cpp -lz -o "$test_dir/zip-archive"
python3 tests/test_zip_archive.py "$test_dir/zip-archive" "$test_dir"

cc -std=c99 -Wall -Wextra -Isource tests/test_gamefaqs_discovery.c source/gamefaqs_html_native.c source/gamefaqs_page_index.c source/gamefaqs_saves_index.c -o "$test_dir/gamefaqs-discovery"
"$test_dir/gamefaqs-discovery"

cc -std=c99 -Wall -Wextra -Isource tests/test_gamefaqs_store.c source/gamefaqs_index_store.c source/gamefaqs_saves_index.c -o "$test_dir/gamefaqs-store"
"$test_dir/gamefaqs-store" "$test_dir"

cc -std=c99 -Wall -Wextra -Isource tests/test_gamefaqs_pipeline.c source/gamefaqs_discovery_pipeline.c source/gamefaqs_html_native.c source/gamefaqs_page_index.c source/gamefaqs_index_store.c source/gamefaqs_saves_index.c -o "$test_dir/gamefaqs-pipeline"
"$test_dir/gamefaqs-pipeline" "$test_dir"
