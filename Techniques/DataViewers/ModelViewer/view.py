import Host
import GigiArray
import sys
import os
import numpy

BoundsBuffer = "VertexBuffer"

#Host.Print("Argc: " + str(len(sys.argv)))
#Host.Print("Argv: " + str(sys.argv))

Host.LoadGG("ModelViewer.gg")

fileName = sys.argv[0]
Host.SetImportedBufferFile("VertexBuffer", fileName)
Host.SetImportedBufferFile("LightBuffer", fileName)
Host.SetImportedBufferFile("MaterialBuffer", fileName)
Host.SetImportedBufferMaterialShaderFile("MaterialBuffer", "_material.hlsli")

Host.SetCameraPos(0,0,0)
Host.SetCameraAltitudeAzimuth(0, 3.14)
Host.RunTechnique(2)
Host.WaitOnGPU()

bounds = Host.GetImportedBufferBounds(BoundsBuffer)

minX = bounds[0]
minY = bounds[1]
minZ = bounds[2]
maxX = bounds[3]
maxY = bounds[4]
maxZ = bounds[5]

Host.SetCameraPos((minX + maxX) / 2, (minY + maxY) / 2, maxZ * 5)

ZRange = max((maxZ-minZ), 1.0)

farZ = ZRange*20
nearZ = farZ / 10000
Host.SetCameraNearFarZ(nearZ, farZ)
Host.SetCameraFlySpeed(ZRange / 25)

Host.SetVariable("MaxTPT", str(farZ))

Host.SetViewedResource("PathTrace.Output: ColorU8sRGB (UAV - After)")
