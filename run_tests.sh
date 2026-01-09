#!/bin/bash

# run_tests.sh - Integration test script for TCP Group Chat

set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}╔════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║  TCP Group Chat - Integration Tests   ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════╝${NC}"
echo ""

# Clean up any previous test artifacts
echo -e "${YELLOW}[1/5]${NC} Cleaning up previous test artifacts..."
rm -f server.pid server.log client_*.log test_*.log
killall server 2>/dev/null || true
sleep 1

# Build the project
echo -e "${YELLOW}[2/5]${NC} Building project..."
make clean > /dev/null 2>&1
make > /dev/null 2>&1
echo -e "${GREEN}✓${NC} Build successful"

# Start server
echo -e "${YELLOW}[3/5]${NC} Starting server on port 8080..."
./server 8080 10 > server.log 2>&1 &
SERVER_PID=$!
echo $SERVER_PID > server.pid
sleep 1

# Check if server is running
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}✗${NC} Server failed to start"
    cat server.log
    exit 1
fi
echo -e "${GREEN}✓${NC} Server started (PID: $SERVER_PID)"

# Start multiple clients
echo -e "${YELLOW}[4/5]${NC} Starting test clients..."

./client 127.0.0.1 8080 alice 10 client_alice.log > /dev/null 2>&1 &
CLIENT1_PID=$!

./client 127.0.0.1 8080 bob 10 client_bob.log > /dev/null 2>&1 &
CLIENT2_PID=$!

./client 127.0.0.1 8080 charlie 10 client_charlie.log > /dev/null 2>&1 &
CLIENT3_PID=$!

echo -e "${GREEN}✓${NC} Started 3 clients (alice, bob, charlie)"

# Wait for clients to finish
echo -e "${YELLOW}[5/5]${NC} Waiting for clients to complete..."
wait $CLIENT1_PID 2>/dev/null || true
wait $CLIENT2_PID 2>/dev/null || true
wait $CLIENT3_PID 2>/dev/null || true

sleep 2

# Verify results
echo ""
echo "─────────────────────────────────────────"
echo "Test Results:"
echo "─────────────────────────────────────────"

PASS_COUNT=0
FAIL_COUNT=0

# Check if log files were created
for client in alice bob charlie; do
    if [ -f "client_${client}.log" ]; then
        lines=$(wc -l < "client_${client}.log")
        if [ $lines -gt 0 ]; then
            echo -e "${GREEN}✓${NC} ${client}: $lines messages logged"
            PASS_COUNT=$((PASS_COUNT + 1))
        else
            echo -e "${RED}✗${NC} ${client}: No messages logged"
            FAIL_COUNT=$((FAIL_COUNT + 1))
        fi
    else
        echo -e "${RED}✗${NC} ${client}: Log file not found"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
done

# Check for join/leave notifications
echo ""
echo "Message Verification:"
for client in alice bob charlie; do
    if grep -q "joined the chat" "client_${client}.log" 2>/dev/null; then
        echo -e "${GREEN}✓${NC} ${client} received join notifications"
    else
        echo -e "${YELLOW}⚠${NC} ${client} may not have received join notifications"
    fi
done

# Sample messages
echo ""
echo "Sample Messages from alice.log:"
echo "─────────────────────────────────────────"
head -n 5 client_alice.log 2>/dev/null || echo "No messages"

# Cleanup
echo ""
echo "─────────────────────────────────────────"
echo "Cleaning up..."
kill $SERVER_PID 2>/dev/null || true
rm -f server.pid

# Final summary
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if [ $FAIL_COUNT -eq 0 ]; then
    echo -e "${GREEN}✓ ALL TESTS PASSED${NC} ($PASS_COUNT/$PASS_COUNT)"
    echo ""
    echo "Log files available for inspection:"
    echo "  - server.log"
    echo "  - client_alice.log"
    echo "  - client_bob.log"
    echo "  - client_charlie.log"
    exit 0
else
    echo -e "${RED}✗ SOME TESTS FAILED${NC} ($PASS_COUNT passed, $FAIL_COUNT failed)"
    echo ""
    echo "Check log files for details:"
    echo "  - server.log"
    exit 1
fi
