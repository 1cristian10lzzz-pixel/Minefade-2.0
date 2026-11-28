#!/usr/bin/env python3
"""Regression checks for the Android NSD discovery lifecycle."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "app/src/main/java/com/minefade/nativeengine/LanMultiplayerService.java").read_text(encoding="utf-8")


def method_body(signature, following):
    start = SOURCE.index(signature)
    end = SOURCE.index(following, start)
    return SOURCE[start:end]


def test_discovery_generation_invalidates_stale_resolve_callbacks():
    assert "private final AtomicInteger discoveryGeneration" in SOURCE
    discover = method_body("public void discover(Listener callback)", "public void join(")
    assert "final int discoveryEpoch = discoveryGeneration.get();" in discover
    resolved = discover[discover.index("public void onServiceResolved"):discover.index("});", discover.index("public void onServiceResolved"))]
    assert "if (discoveryEpoch != discoveryGeneration.get()) return;" in resolved
    assert "discoveryGeneration.incrementAndGet();" in method_body("private void stopDiscovery()", "private void stopSession()")
    notify = method_body("private void notifyGames()", "private void postStatus(")
    assert "discoveryEpoch != discoveryGeneration.get()" in notify
    host = method_body("public void startHost(", "public void discover(")
    assert host.index("stopDiscovery();") < host.index("stopSession();")


def test_nsd_attributes_are_decoded_from_bounded_byte_arrays():
    start = SOURCE.index("private static String readNsdAttribute(")
    helper = SOURCE[start:start + 650]
    assert "Map<String, byte[]> attributes = service.getAttributes();" in helper
    assert "value.length > 128" in helper
    assert "new String(value, StandardCharsets.UTF_8)" in helper
    discover = method_body("public void discover(Listener callback)", "public void join(")
    assert 'readNsdAttribute(info, "protocol")' in discover
    assert 'readNsdAttribute(info, "content")' in discover
    assert 'String fingerprint = info.getAttributes().get("content");' not in discover


def test_discovery_failures_release_the_multicast_lock():
    discover = method_body("public void discover(Listener callback)", "public void join(")
    null_check = discover.index("if (nsdManager == null)")
    acquire = discover.index("acquireMulticastLock();")
    assert acquire > null_check
    catch_at = discover.index('Log.e(TAG, "NSD discovery failed", error);')
    cleanup_at = discover.index("stopDiscovery();", catch_at)
    status_at = discover.index("postStatus(", catch_at)
    assert cleanup_at < status_at


if __name__ == "__main__":
    test_discovery_generation_invalidates_stale_resolve_callbacks()
    test_nsd_attributes_are_decoded_from_bounded_byte_arrays()
    test_discovery_failures_release_the_multicast_lock()
    print("Minefade LAN discovery lifecycle regressions passed.")
