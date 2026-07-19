all:
	cd tree-sitter-ga; tree-sitter generate -o ../src
	cmake -S src -B build
	make -C build
	build/gac test/test.ga
