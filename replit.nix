{ pkgs }: {
	deps = [
		pkgs.clang_12
		pkgs.ccls
		pkgs.gdb
		pkgs.gnumake
        pkgs.boost
        pkgs.netcat-gnu
        pkgs.lua5_4
        pkgs.rlwrap
        pkgs.vim
        pkgs.valgrind
	];
}