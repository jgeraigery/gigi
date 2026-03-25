import importlib
import sys

sys.path.append('Techniques/UnitTests/')
m = importlib.import_module("TestLogic")

resources = [
	[ "Buffer.resource - Initial State", False ],
]

def DoTest():
	return m.RunATest("Data/ply_cube_text_nonFlattened", resources)

# This is so the test can be ran by itself directly
if __name__ == "builtins":
	DoTest()
