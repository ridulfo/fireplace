{
  stdenv,
  ncurses,
}:
stdenv.mkDerivation {
  pname = "fireplace";
  version = "1.0.0";
  src = ./.;
  buildInputs = [
    ncurses
  ];
  makeFlags = [ "DESTDIR=$(out)/bin" ];
}
