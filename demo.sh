#!/bin/bash

# Clean, visual demo for GitHub screenshots

GREEN='\033[0;32m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

clear

echo -e "${CYAN}═══════════════════════════════════════════════════════════${NC}"
echo -e "${CYAN}          TCP GROUP CHAT - LIVE DEMO${NC}"
echo -e "${CYAN}═══════════════════════════════════════════════════════════${NC}"
echo ""

# Clean up
pkill -9 server client 2>/dev/null
rm -f *.log *.pid 2>/dev/null

# Build
echo -e "${BLUE}Building...${NC}"
make clean > /dev/null 2>&1
make > /dev/null 2>&1
echo -e "${GREEN}✓ Build complete${NC}"
echo ""

# Start server
echo -e "${BLUE}Starting server on port 8080...${NC}"
./server 8080 10 > server.log 2>&1 &
SERVER_PID=$!
sleep 1
echo -e "${GREEN}✓ Server running${NC}"
echo ""

# Start clients
echo -e "${BLUE}Connecting clients...${NC}"
./client 127.0.0.1 8080 alice 5 alice.log > /dev/null 2>&1 &
sleep 0.3
./client 127.0.0.1 8080 bob 5 bob.log > /dev/null 2>&1 &
sleep 0.3
./client 127.0.0.1 8080 charlie 5 charlie.log > /dev/null 2>&1 &
sleep 0.3
echo -e "${GREEN}✓ alice, bob, charlie connected${NC}"
echo ""

# Wait for messages
echo -e "${BLUE}Exchanging messages...${NC}"
sleep 2

# Show output
echo ""
echo -e "${CYAN}─────────────────────────────────────────────────────────${NC}"
echo -e "${CYAN}  Chat Log (alice's view)${NC}"
echo -e "${CYAN}─────────────────────────────────────────────────────────${NC}"
head -n 15 alice.log
echo ""

# Stats
echo -e "${CYAN}─────────────────────────────────────────────────────────${NC}"
echo -e "${CYAN}  Statistics${NC}"
echo -e "${CYAN}─────────────────────────────────────────────────────────${NC}"
for client in alice bob charlie; do
    msgs=$(grep -c "\[.*@.*:.*\]" ${client}.log 2>/dev/null || echo 0)
    echo -e "${GREEN}${client}:${NC} $msgs messages received"
done
echo ""

# Cleanup
kill $SERVER_PID 2>/dev/null
echo -e "${GREEN}✓ Demo complete${NC}"
echo ""
