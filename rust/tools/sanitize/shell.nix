# Native NixOS environment for sanitize.sh: the nixpkgs OBS build inputs plus
# cmocka, valgrind and a headless X server. Usage: nix-shell rust/tools/sanitize/shell.nix
{ pkgs ? import <nixpkgs> { } }:
pkgs.mkShell {
  inputsFrom = [ pkgs.obs-studio ];
  packages = with pkgs; [
    cmocka valgrind xorg.xorgserver dbus mesa ninja ccache git rsync
    simde uthash nlohmann_json websocketpp asio qrcodegencpp
  ];
  hardeningDisable = [ "all" ];
  shellHook = ''
    export QT_PLUGIN_PATH=${pkgs.qt6.qtbase}/${pkgs.qt6.qtbase.qtPluginPrefix}:${pkgs.qt6.qtsvg}/${pkgs.qt6.qtbase.qtPluginPrefix}:${pkgs.qt6.qtwayland}/${pkgs.qt6.qtbase.qtPluginPrefix}
    export LIBGL_DRIVERS_PATH=${pkgs.mesa}/lib/dri
    export __EGL_VENDOR_LIBRARY_FILENAMES=${pkgs.mesa}/share/glvnd/egl_vendor.d/50_mesa.json
    export LD_LIBRARY_PATH=${pkgs.libglvnd}/lib:${pkgs.mesa}/lib''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
  '';
}
