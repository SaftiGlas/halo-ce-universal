#!/bin/sh
# A scripted Forge host and client on this machine, without the menus
# (debug.network_test and debug.forge_test_edit, port/linux/README.md): the
# host spawns a scenery object, the client asks it to spawn one, to move it
# and to remove it. Run it from a desktop session; the client joins through
# the invite link the host puts on the clipboard.
#
#     tools/forge_net_test.sh [SECONDS]      (default 75)
#     HALO_BIN=build/mods/<key>/halo tools/forge_net_test.sh
#
# The logs are build/forge_test/t_host.log and t_client.log. A good run has,
# on the host, "forge layout: a client asks to spawn / move / remove ..." and
# after each, on the client, "forge layout: N bytes from the host".
cd "$(dirname "$0")/.." || exit 1
T=build/forge_test
BIN=${HALO_BIN:-./build/linux/halo}
SECONDS_TO_RUN=${1:-75}
mkdir -p $T/t_host $T/t_client
COMMON="SDL_VIDEODRIVER=x11 HALO_FORGE_TEST_EDIT=1 HALO_EXIT_AFTER=$SECONDS_TO_RUN"
env $COMMON HALO_NETWORK_TEST=host:bloodgulch:forge HALO_NETWORK_TEST_START=14 HALO_TEST_INPUT=look:1 \
	HALO_SAVE_ROOT="$PWD/$T/t_host" "$BIN" > $T/t_host.log 2>&1 &
sleep 6
env $COMMON HALO_NETWORK_TEST=join HALO_TEST_INPUT=look:2 \
	HALO_SAVE_ROOT="$PWD/$T/t_client" "$BIN" > $T/t_client.log 2>&1
wait
for name in t_host t_client; do
	echo "== $name"
	grep "forge" $T/$name.log | grep -v "network test: tick"
done
if grep -q "a client asks to remove" $T/t_host.log && [ "$(grep -c "bytes from the host" $T/t_client.log)" -ge 4 ]; then
	echo "forge net test: passed"
else
	echo "forge net test: FAILED (see $T/t_host.log and $T/t_client.log)"
	exit 1
fi
