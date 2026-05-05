#!/usr/bin/env python3
"""
Simple test script for MCP server
"""

import subprocess
import json
import sys
import time

# Set to True to show detailed JSON request/response logging
VERBOSE_JSON = False

# Pause duration (in seconds) after sending each request. Useful for debugging
REQUEST_PAUSE_SECONDS = 0.0


def send_request(process, request):
    """Send a JSON-RPC request to the MCP server"""
    request_json = json.dumps(request)
    message = f"Content-Length: {len(request_json)}\r\n\r\n{request_json}"

    # Build the request description
    method_desc = request['method']
    if method_desc == "tools/call" and 'params' in request and 'name' in request['params']:
        method_desc = f"{method_desc} ({request['params']['name']})"

    if VERBOSE_JSON:
        print(f"\nSending request: {method_desc} (ID: {request['id']})")
    if VERBOSE_JSON and 'params' in request:
        print(f"Params: {json.dumps(request['params'], indent=2)}")

    process.stdin.write(message.encode())
    process.stdin.flush()

    if REQUEST_PAUSE_SECONDS > 0:
        time.sleep(REQUEST_PAUSE_SECONDS)


def read_response(process):
    """Read a JSON-RPC response from the MCP server"""
    # Read headers
    content_length = 0
    while True:
        line = process.stdout.readline().decode('utf-8')
        #print(f"DEBUG1: {repr(line)}")
        #print(f"DEBUG1: ASCII values: {[ord(c) for c in line]}")
        if line.startswith('Content-Length:'):
            content_length = int(line.split(':')[1].strip())
        elif line in ['\r\n', '\n', '']:
            break

    # Read content
    if content_length > 0:
        content = process.stdout.read(content_length).decode('utf-8')
        #print(f"DEBUG2: {repr(content)}")
        response = json.loads(content)

        has_error = 'error' in response

        if VERBOSE_JSON:
            print(f"\nReceived response (ID: {response.get('id')})")
        if 'result' in response and VERBOSE_JSON:
            print(f"Result: {json.dumps(response['result'], indent=2)}")
        elif has_error:
            print(f"ERROR: {json.dumps(response['error'], indent=2)}")

        #process.stdout.flush()

        return response

    return None


def test_mcp_server(server_path):
    """Run a series of tests against the MCP server"""
    print(f"Starting MCP server: {server_path}")
    print("=" * 80)

    process = None
    try:
        process = subprocess.Popen(
            [server_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )

        # Test 1: Initialize
        print("\nTest 1: Initialize")
        send_request(process, {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "protocolVersion": "2024-11-05"
            }
        })
        response = read_response(process)
        assert response['id'] == 1
        assert 'result' in response
        print("PASS: Initialize successful")

        # Launch Viewer
        print("\nLaunching Viewer")
        send_request(process, {
            "jsonrpc": "2.0",
            "id": 100,
            "method": "tools/call",
            "params": {
                "name": "LaunchViewer",
                "arguments": {
                    "port": 6161,
                    "commandLine": ""
                }
            }
        })
        response = read_response(process)
        assert response['id'] == 100
        assert 'result' in response
        print("PASS: Viewer launched")

        # SetViewerIP
        print("\nSetting Viewer IP")
        send_request(process, {
            "jsonrpc": "2.0",
            "id": 101,
            "method": "tools/call",
            "params": {
                "name": "SetViewerIP",
                "arguments": {
                    "IP": "127.0.0.1",
                    "port": 6161
                }
            }
        })
        response = read_response(process)
        assert response['id'] == 101
        assert 'result' in response
        print("PASS: Viewer IP set")

        # Ping
        print("\nPing Test")
        send_request(process, {
            "jsonrpc": "2.0",
            "id": 102,
            "method": "tools/call",
            "params": {
                "name": "Ping",
                "arguments": {}
            }
        })
        response = read_response(process)
        assert response['id'] == 102
        assert 'result' in response
        print("PASS: Ping successful")

        # ViewerPing
        print("\nViewerPing Test")
        send_request(process, {
            "jsonrpc": "2.0",
            "id": 103,
            "method": "tools/call",
            "params": {
                "name": "ViewerPing",
                "arguments": {}
            }
        })
        response = read_response(process)
        assert response['id'] == 103
        assert 'result' in response
        print("PASS: ViewerPing successful")

        # Test 2: List tools
        print("\nTest 2: List Tools")
        send_request(process, {
            "jsonrpc": "2.0",
            "id": 2,
            "method": "tools/list"
        })
        response = read_response(process)
        assert response['id'] == 2
        assert 'result' in response
        assert 'tools' in response['result']
        print(f"PASS: Found {len(response['result']['tools'])} tool(s)")

        # Test 3: Set and get camera
        print("\nTest 3: Call SetCameraPos")
        test_camera_pos = {"x": 1.5, "y": 2.5, "z": 3.5}

        # Set camera position
        send_request(process, {
            "jsonrpc": "2.0",
            "id": 3,
            "method": "tools/call",
            "params": {
                "name": "SetCameraPos",
                "arguments": test_camera_pos
            }
        })
        response = read_response(process)
        assert response['id'] == 3
        assert 'result' in response
        print("SetCameraPos successful")

        print("\nTest 3.5: Call GetCameraPos")

        # Get camera position to verify
        send_request(process, {
            "jsonrpc": "2.0",
            "id": 6,
            "method": "tools/call",
            "params": {
                "name": "GetCameraPos",
                "arguments": {}
            }
        })
        response = read_response(process)
        assert response['id'] == 6
        assert 'result' in response

        # Parse the result and verify camera position
        # Extract text from content array and parse as JSON
        text_content = response['result']['content'][0]['text'].strip()
        camera_array = json.loads(text_content)

        assert abs(camera_array[0] - test_camera_pos['x']) < 0.001, f"X mismatch: {camera_array[0]} != {test_camera_pos['x']}"
        assert abs(camera_array[1] - test_camera_pos['y']) < 0.001, f"Y mismatch: {camera_array[1]} != {test_camera_pos['y']}"
        assert abs(camera_array[2] - test_camera_pos['z']) < 0.001, f"Z mismatch: {camera_array[2]} != {test_camera_pos['z']}"
        print(f"PASS: Camera position verified: [{camera_array[0]}, {camera_array[1]}, {camera_array[2]}]")

        # Test 4: Call unknown tool (should error)
        print("\nTest 4: Call Unknown Tool (expect error)")
        send_request(process, {
            "jsonrpc": "2.0",
            "id": 4,
            "method": "tools/call",
            "params": {
                "name": "nonexistent",
                "arguments": {}
            }
        })
        response = read_response(process)
        assert response['id'] == 4
        assert 'error' in response
        print("PASS: Error handling works correctly")

        # Test 5: Unknown method (should error)
        print("\nTest 5: Unknown Method (expect error)")
        send_request(process, {
            "jsonrpc": "2.0",
            "id": 5,
            "method": "unknown/method"
        })
        response = read_response(process)
        assert response['id'] == 5
        assert 'error' in response
        print("PASS: Unknown method handled correctly")

        print("\n" + "=" * 80)
        print("ALL TESTS PASSED!")

    except subprocess.TimeoutExpired:
        process.kill()
        print("\nERROR: Server did not terminate in time")
        sys.exit(1)
    except AssertionError as e:
        print(f"\nERROR: Test failed: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"\nERROR: Unexpected error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
    finally:
        # Always send Exit command before terminating
        if process:
            try:
                print("\nSending Exit command")
                send_request(process, {
                    "jsonrpc": "2.0",
                    "id": 999,
                    "method": "tools/call",
                    "params": {
                        "name": "Exit",
                        "arguments": {
                            "exitCode": 0
                        }
                    }
                })
                response = read_response(process)
                print("Exit command sent")
            except Exception as e:
                print(f"Failed to send Exit command: {e}")

            try:
                process.terminate()
                process.wait(timeout=5)
            except:
                process.kill()


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python test_simple.py <path_to_mcp_server_executable>")
        sys.exit(1)

    server_path = sys.argv[1]
    test_mcp_server(server_path)
