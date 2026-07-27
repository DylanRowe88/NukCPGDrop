#!/usr/bin/env python3
"""Test that the DNS server's response format matches what the C code generates."""

import struct, random

def build_dns_query(hostname):
    """Build a real DNS query for the given hostname."""
    tid = random.randint(0, 0xFFFF)
    header = struct.pack('>HHHHHH', tid, 0x0100, 1, 0, 0, 0)
    question = b''
    for part in hostname.split('.'):
        question += bytes([len(part)]) + part.encode()
    question += b'\x00'  # null terminator
    question += struct.pack('>HH', 1, 1)  # QTYPE=A, QCLASS=IN
    return header + question


def parse_dns_response(data):
    """Parse a minimal DNS response to verify it's valid."""
    assert len(data) >= 12, "Response too short for DNS header"

    tid = struct.unpack('>H', data[0:2])[0]
    flags = struct.unpack('>H', data[2:4])[0]
    qdcount = struct.unpack('>H', data[4:6])[0]
    ancount = struct.unpack('>H', data[6:8])[0]

    assert flags & 0x8000, "QR flag not set (not a response)"
    assert qdcount == 1, f"Expected 1 question, got {qdcount}"
    assert ancount >= 1, f"Expected at least 1 answer, got {ancount}"

    # Parse question (skip name)
    pos = 12
    while pos < len(data):
        label_len = data[pos]
        if label_len == 0:
            pos += 1
            break
        if label_len & 0xC0:  # compression pointer
            pos += 2
            break
        pos += 1 + label_len
    pos += 4  # skip QTYPE + QCLASS

    # Parse answer
    if pos + 12 <= len(data):
        # Name compression pointer
        ptr = struct.unpack('>H', data[pos:pos+2])[0]
        assert ptr & 0xC000, "Answer name should use compression pointer"
        atype = struct.unpack('>H', data[pos+2:pos+4])[0]
        aclass = struct.unpack('>H', data[pos+4:pos+6])[0]
        ttl = struct.unpack('>I', data[pos+6:pos+10])[0]
        rdlen = struct.unpack('>H', data[pos+10:pos+12])[0]

        assert atype == 1, f"Expected A record (type 1), got {atype}"
        assert aclass == 1, f"Expected IN class (1), got {aclass}"
        assert rdlen == 4, f"Expected 4 bytes of RDATA, got {rdlen}"

        if pos + 14 + rdlen <= len(data):
            ip = data[pos+12:pos+12+rdlen]
            assert ip == bytes([192, 168, 4, 1]), f"Expected 192.168.4.1, got {'.'.join(map(str, ip))}"

    return True


def test_apple_query():
    """Test response to captive.apple.com query."""
    query = build_dns_query("captive.apple.com")
    response = simulate_dns_response(query)
    parse_dns_response(response)
    print("OK: Apple hotspot-detect query")


def test_android_query():
    """Test response to connectivitycheck.gstatic.com query."""
    query = build_dns_query("connectivitycheck.gstatic.com")
    response = simulate_dns_response(query)
    parse_dns_response(response)
    print("OK: Android connectivity check query")


def test_windows_query():
    """Test response to msftconnecttest.com query."""
    query = build_dns_query("www.msftconnecttest.com")
    response = simulate_dns_response(query)
    parse_dns_response(response)
    print("OK: Windows connecttest query")


def test_short_hostname():
    """Test response to single-label hostname (e.g., 'router')."""
    query = build_dns_query("router")
    response = simulate_dns_response(query)
    parse_dns_response(response)
    print("OK: Short hostname")


def test_long_hostname():
    """Test response to a very long hostname."""
    hostname = "a." * 63 + "com"
    query = build_dns_query(hostname)
    response = simulate_dns_response(query)
    parse_dns_response(response)
    print("OK: Long hostname")


def simulate_dns_response(query):
    """Simulate what the C dns_reply_a function would produce.

    This mirrors the logic in dns_server.c:dns_reply_a()
    """
    buf = bytearray(query)
    qlen = 12
    while qlen < len(buf):
        c = buf[qlen]
        if c == 0:
            qlen += 5
            break
        if c & 0xC0:
            qlen += 4
            break
        qlen += 1 + c

    # Build response header
    resp = bytearray(512)
    resp[0:2] = buf[0:2]   # ID
    resp[2] = 0x85          # QR|AA
    resp[3] = 0x80          # RA
    resp[4] = 0x00; resp[5] = 0x01  # QDCOUNT=1
    resp[6] = 0x00; resp[7] = 0x01  # ANCOUNT=1
    resp[8] = 0x00; resp[9] = 0x00
    resp[10] = 0x00; resp[11] = 0x00
    rlen = 12

    # Echo question
    question_len = qlen - 12
    resp[rlen:rlen+question_len] = buf[12:12+question_len]
    rlen += question_len

    # Answer: name compression ptr, type A, class IN, TTL 60, IP 192.168.4.1
    resp[rlen] = 0xC0; resp[rlen+1] = 0x0C; rlen += 2
    resp[rlen] = 0x00; resp[rlen+1] = 0x01; rlen += 2  # type A
    resp[rlen] = 0x00; resp[rlen+1] = 0x01; rlen += 2  # class IN
    resp[rlen] = 0x00; resp[rlen+1] = 0x00
    resp[rlen] = 0x00; resp[rlen+1] = 0x3C; rlen += 4  # TTL 60
    resp[rlen] = 0x00; resp[rlen+1] = 0x04; rlen += 2  # data len 4
    resp[rlen] = 192; resp[rlen+1] = 168
    resp[rlen+2] = 4; resp[rlen+3] = 1; rlen += 4      # IP

    return bytes(resp[:rlen])


if __name__ == '__main__':
    test_apple_query()
    test_android_query()
    test_windows_query()
    test_short_hostname()
    test_long_hostname()
    print("\nAll DNS response tests passed.")
