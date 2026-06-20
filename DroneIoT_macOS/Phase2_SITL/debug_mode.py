import time
from pymavlink import mavutil

print("Connecting to SITL...")
master = mavutil.mavlink_connection('tcp:127.0.0.1:5763')
master.wait_heartbeat()
print(f"Connected to system {master.target_system}")

print("Requesting GUIDED mode...")
master.set_mode('GUIDED')

# Read ACK
while True:
    msg = master.recv_match(type=['COMMAND_ACK'], blocking=True, timeout=5)
    if not msg:
        print("Timeout waiting for ACK")
        break
    
    if msg.command == mavutil.mavlink.MAV_CMD_DO_SET_MODE:
        print(f"ACK for SET_MODE received: result={msg.result}")
        if msg.result == 0:
            print("SUCCESS! Changed to GUIDED.")
        else:
            print(f"FAILED! Error code: {msg.result}")
        break
