#include <gfd.h>
#include <gx2/draw.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/shaders.h>
#include <gx2r/buffer.h>
#include <gx2r/draw.h>
#include <stdio.h>
#include <string.h>
#include <whb/file.h>
#include <whb/gfx.h>
#include <whb/log.h>
#include <whb/log_udp.h>
#include <whb/proc.h>
#include <whb/sdcard.h>
#include <math.h>
#include <vpad/input.h>

#include <coreinit/systeminfo.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>

static const float sPositionData[] = {
   // clang-format off
    1.0f, -1.0f,  0.0f, 1.0f,
    0.0f,  1.0f,  0.0f, 1.0f,
   -1.0f, -1.0f,  1.0f, 1.0f,
   // clang-format on
};

static const float sColourData[] = {
   // clang-format off
   1.0f,  0.0f,  0.0f, 1.0f,
   0.0f,  1.0f,  0.0f, 1.0f,
   0.0f,  0.0f,  1.0f, 1.0f,
   // clang-format on
};

float clampf(float d, float min, float max) {
  const float t = d < min ? min : d;
  return t > max ? max : t;
}

int
main(int argc, char **argv)
{
   GX2RBuffer positionBuffer = {0};
   GX2RBuffer colourBuffer   = {0};
   WHBGfxShaderGroup group   = {0};
   void *buffer              = NULL;
   char *gshFileData         = NULL;
   char path[256];
   int result = 0;
   float angle = 0.0f;

   float gyroX = 0.0f;
   float gyroY = 0.0f;
   float gyroZ = 0.0f;
   float stickX = 0.0f;
   float stickY = 0.0f;
   float rStickX = 0.0f;
   float rStickY = 0.0f;

   WHBLogUdpInit();
   WHBProcInit();
   WHBGfxInit();

   if (!WHBMountSdCard()) {
      result = -1;
      goto exit;
   }

   gshFileData = WHBReadWholeFile("/vol/content/pos_col_shader.gsh", NULL);
   if (!gshFileData) {
      result = -1;
      WHBLogPrintf("WHBReadWholeFile(%s) returned NULL", path);
      goto exit;
   }

   if (!WHBGfxLoadGFDShaderGroup(&group, 0, gshFileData)) {
      result = -1;
      WHBLogPrintf("WHBGfxLoadGFDShaderGroup returned FALSE");
      goto exit;
   }

   WHBGfxInitShaderAttribute(&group, "aPosition", 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32);
   WHBGfxInitShaderAttribute(&group, "aColour", 1, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32);
   WHBGfxInitFetchShader(&group);

   WHBFreeWholeFile(gshFileData);
   gshFileData          = NULL;

   // Set vertex position
   positionBuffer.flags = GX2R_RESOURCE_BIND_VERTEX_BUFFER |
                          GX2R_RESOURCE_USAGE_CPU_READ |
                          GX2R_RESOURCE_USAGE_CPU_WRITE |
                          GX2R_RESOURCE_USAGE_GPU_READ;
   positionBuffer.elemSize  = 4 * 4;
   positionBuffer.elemCount = 3;
   GX2RCreateBuffer(&positionBuffer);
   buffer = GX2RLockBufferEx(&positionBuffer, 0);
   memcpy(buffer, sPositionData, positionBuffer.elemSize * positionBuffer.elemCount);
   GX2RUnlockBufferEx(&positionBuffer, 0);

   // Set vertex colour
   colourBuffer.flags = GX2R_RESOURCE_BIND_VERTEX_BUFFER |
                        GX2R_RESOURCE_USAGE_CPU_READ |
                        GX2R_RESOURCE_USAGE_CPU_WRITE |
                        GX2R_RESOURCE_USAGE_GPU_READ;
   colourBuffer.elemSize  = 4 * 4;
   colourBuffer.elemCount = 3;
   GX2RCreateBuffer(&colourBuffer);
   buffer = GX2RLockBufferEx(&colourBuffer, 0);
   memcpy(buffer, sColourData, colourBuffer.elemSize * colourBuffer.elemCount);
   GX2RUnlockBufferEx(&colourBuffer, 0);

   WHBLogPrintf("Begin rendering...");
   while (WHBProcIsRunning()) {

    VPADStatus status;
    VPADReadError err;
    VPADRead(VPAD_CHAN_0, &status, 1, &err);
    stickX += status.leftStick.x * 0x7ff0;
    stickY -= status.leftStick.y * 0x7ff0;

    rStickX += status.rightStick.x * 0x7ff0;
    rStickY -= status.rightStick.y * 0x7ff0;

    gyroZ = -0x20000 * status.gyro.z;
    gyroX = 0x20000 * status.gyro.x;
    gyroY = 0x20000 * status.gyro.y;

    static int frameCount = 0;
    frameCount++;
    if (frameCount % 30 == 0) {
        WHBLogPrintf("gyro: %f %f %f", gyroX, gyroY, gyroZ);
    }

    //     1.0f, -1.0f,  0.0f, 1.0f, BR
    //     0.0f,  1.0f,  0.0f, 1.0f, TC
    //    -1.0f, -1.0f,  1.0f, 1.0f, BL
    static float pitch = 0, yaw = 0, roll = 0;
    pitch += status.gyro.x * 0.1f;
    yaw   += status.gyro.y * 0.1f;
    roll  += status.gyro.z * 0.1f;

    float cp = cosf(pitch), sp = sinf(pitch);
    float cy = cosf(yaw),   sy = sinf(yaw);
    float cr = cosf(roll),  sr = sinf(roll);

    // base triangle verts
    float verts[3][3] = {
        { 1.0f, -1.0f, 0.0f },
        { 0.0f,  1.0f, 0.0f },
        {-1.0f, -1.0f, 0.0f },
    };

    float *triangle = (float *)GX2RLockBufferEx(&positionBuffer, 0);

    triangle[0] = ((1.0f * cy) - (-1.0f * sy)) * cr;
    triangle[1] = ((1.0f * sy) + (-1.0f * cy)) * cp;
    triangle[2] = -1.0f * sp * sr;
    triangle[3] = 2.0f;

    triangle[4] = ((0.0f * cy) - (1.0f * sy));
    triangle[5] = ((0.0f * sy) + (1.0f * cy)) * cp;
    triangle[6] = -1.0f * sp;
    triangle[7] = 2.0f;

    triangle[8] = ((-1.0f * cy) - (-1.0f * sy)) * cr;
    triangle[9] = ((-1.0f * sy) + (-1.0f * cy)) * cp;
    triangle[10] = 1.0f * sp * sr;
    triangle[11] = 2.0f;
    GX2RUnlockBufferEx(&positionBuffer, 0);

    // Render!
    WHBGfxBeginRender();

    WHBGfxBeginRenderTV();
    WHBGfxClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    GX2SetFetchShader(&group.fetchShader);
    GX2SetVertexShader(group.vertexShader);
    GX2SetPixelShader(group.pixelShader);
    GX2RSetAttributeBuffer(&positionBuffer, 0, positionBuffer.elemSize, 0);
    GX2RSetAttributeBuffer(&colourBuffer, 1, colourBuffer.elemSize, 0);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 3, 0, 1);
    WHBGfxFinishRenderTV();

    WHBGfxBeginRenderDRC();
    WHBGfxClearColor(1.0f, 0.0f, 1.0f, 1.0f);
    GX2SetFetchShader(&group.fetchShader);
    GX2SetVertexShader(group.vertexShader);
    GX2SetPixelShader(group.pixelShader);
    GX2RSetAttributeBuffer(&positionBuffer, 0, positionBuffer.elemSize, 0);
    GX2RSetAttributeBuffer(&colourBuffer, 1, colourBuffer.elemSize, 0);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 3, 0, 1);
    WHBGfxFinishRenderDRC();

    WHBGfxFinishRender();
}

exit:
   WHBLogPrintf("Exiting...");
   GX2RDestroyBufferEx(&positionBuffer, 0);
   GX2RDestroyBufferEx(&colourBuffer, 0);
   WHBUnmountSdCard();
   WHBGfxShutdown();
   WHBProcShutdown();
   WHBLogUdpDeinit();
   return result;
}
