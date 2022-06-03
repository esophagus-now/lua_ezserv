{ pkgs }: {
	deps = [
		pkgs.clang_12
		pkgs.ccls
		pkgs.gdb
		pkgs.gnumake
        pkgs.boost
        pkgs.netcat-gnu
        pkgs.lua5_1
        pkgs.rlwrap
        pkgs.vim
        pkgs.valgrind
        pkgs.less
        pkgs.luarocks
	];
}