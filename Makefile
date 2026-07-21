all: run

tsgen:
	tree-sitter generate

build: tsgen
	cmake -S src -B build
	make -C build

run: build
	build/gac test/test.ga

clean:
	rm -rf build
	rm src/parser.c
	rm src/grammar.json
	rm src/node-types.json
	rm -rf src/tree_sitter
