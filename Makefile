all: tsgen build run

tsgen:
	cd tree-sitter-ga; tree-sitter generate -o ../src

build:
	cmake -S src -B build
	make -C build

run:
	build/gac test/test.ga

clean:
	rm -rf Build
