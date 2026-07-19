all: run

tsgen:
	cd tree-sitter-ga; tree-sitter generate -o ../src

build: tsgen
	cmake -S src -B build
	make -C build

run: build
	build/gac test/test.ga

clean:
	rm -rf Build
