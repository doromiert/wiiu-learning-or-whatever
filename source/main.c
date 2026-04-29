#include <gfd.h>
#include <gx2/draw.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/shaders.h>
#include <gx2r/buffer.h>
#include <gx2r/draw.h>
#include <stdlib.h>
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

#define MAX(value1, value2) (value1 > value2 ? value1 : value2)
#define MIN(value1, value2) (value1 < value2 ? value1 : value2)

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

typedef struct {
    float x0, x1, y0, y1;
} Rect;

typedef struct {
    float r, g, b, a;
} Color;

typedef struct {
    float x, y, z, w;
} Vert;

typedef struct {
    Vert v0;
    Vert v1;
    Vert v2;
} Triangle;

typedef struct {
    GX2RBuffer rBuffer, cBuffer;
    Rect coords;
    char color[8];
} Rectangle;

float clampf(float d, float min, float max) {
  const float t = d < min ? min : d;
  return t > max ? max : t;
}

Color toGx2Color(char* color){
    bool isHash = (color[0] == '#');
    long cNum = strtol(color + isHash, NULL, 16);

    if((strlen(color) - isHash) == 6){
        return (Color){
            ((cNum >> 16) & 0xFF) / 255.0f,
            ((cNum >> 8) & 0xFF) / 255.0f,
            (cNum & 0xFF) / 255.0f,
            255.0f / 255.0f
        };
    }else{
        return (Color){
            ((cNum >> 24) & 0xFF) / 255.0f,
            ((cNum >> 16) & 0xFF) / 255.0f,
            ((cNum >> 8) & 0xFF) / 255.0f,
            (cNum & 0xFF) / 255.0f,
        };
    }
}

void drawRect(Rect rect, char* color, GX2RBuffer *rectBuffer, GX2RBuffer *colorBuffer, WHBGfxShaderGroup *group){
    Color gx2Color = toGx2Color(color);

    float nX0 = clampf((rect.x0 / 854.0f) * 2 - 1, -1.0f, 1.0f);
    float nY0 = -1.0f * clampf((rect.y0 / 480.0f) * 2 - 1, -1.0f, 1.0f);
    float nX1 = clampf((rect.x1 / 854.0f) * 2 - 1, -1.0f, 1.0f);
    float nY1 = -1.0f * clampf((rect.y1 / 480.0f) * 2 - 1, -1.0f, 1.0f);

    Triangle tri0 = {
        (Vert){ nX0, nY0, 0.0f, 1.0f },
        (Vert){ nX1, nY0, 0.0f, 1.0f },
        (Vert){ nX0, nY1, 0.0f, 1.0f }
    };

    Triangle tri1 = {
        (Vert){ nX1, nY1, 0.0f, 1.0f },
        (Vert){ nX1, nY0, 0.0f, 1.0f },
        (Vert){ nX0, nY1, 0.0f, 1.0f }
    };

    void *rBuf = GX2RLockBufferEx(rectBuffer, 0);
    void *cBuf = GX2RLockBufferEx(colorBuffer, 0);

    memcpy(rBuf, &tri0, sizeof(Triangle));
    memcpy((char*)rBuf + sizeof(Triangle), &tri1, sizeof(Triangle));

    float colorData[6 * 4] = {
        gx2Color.r, gx2Color.g, gx2Color.b, gx2Color.a,
        gx2Color.r, gx2Color.g, gx2Color.b, gx2Color.a,
        gx2Color.r, gx2Color.g, gx2Color.b, gx2Color.a,
        gx2Color.r, gx2Color.g, gx2Color.b, gx2Color.a,
        gx2Color.r, gx2Color.g, gx2Color.b, gx2Color.a,
        gx2Color.r, gx2Color.g, gx2Color.b, gx2Color.a,
    };

    memcpy(cBuf, &colorData, sizeof(colorData));

    GX2RUnlockBufferEx(rectBuffer, 0);
    GX2RUnlockBufferEx(colorBuffer, 0);

    GX2SetFetchShader(&group->fetchShader);
    GX2SetVertexShader(group->vertexShader);
    GX2SetPixelShader(group->pixelShader);
    GX2RSetAttributeBuffer(rectBuffer, 0, rectBuffer->elemSize, 0);
    GX2RSetAttributeBuffer(colorBuffer, 1, colorBuffer->elemSize, 0);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 6, 0, 1);

    return;
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

   Rectangle testRect = {
       {0},
       {0},
       {
          277, 577, 140, 340
       },
       "#000000"
   };

   float gyroX = 0.0f;
   float gyroY = 0.0f;
   float gyroZ = 0.0f;
   float stickX = 0.0f;
   float stickY = 0.0f;
   float rStickX = 0.0f;
   float rStickY = 0.0f;

   float size = 1.0f;

   float tX = 1.0f;
   float tY = 1.0f;

   float touchX = 0.0f;
   float touchY = 0.0f;
   bool touchDown = false;

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

   // Set vertex position
   testRect.rBuffer.flags = GX2R_RESOURCE_BIND_VERTEX_BUFFER |
                            GX2R_RESOURCE_USAGE_CPU_READ |
                            GX2R_RESOURCE_USAGE_CPU_WRITE |
                            GX2R_RESOURCE_USAGE_GPU_READ;
   testRect.rBuffer.elemSize  = 4 * 4;
   testRect.rBuffer.elemCount = 6;
   GX2RCreateBuffer(&testRect.rBuffer);

   // Set vertex colour
   testRect.cBuffer.flags = GX2R_RESOURCE_BIND_VERTEX_BUFFER |
                        GX2R_RESOURCE_USAGE_CPU_READ |
                        GX2R_RESOURCE_USAGE_CPU_WRITE |
                        GX2R_RESOURCE_USAGE_GPU_READ;
   testRect.cBuffer.elemSize  = 4 * 4;
   testRect.cBuffer.elemCount = 6;
   GX2RCreateBuffer(&testRect.cBuffer);

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

    touchX = status.tpNormal.x;
    touchY = status.tpNormal.y;
    touchDown = status.tpNormal.touched;

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

    size = 1.0f + rStickY * -0.00001f;

    tX = -1.0f + touchX / 2000.0f;
    tY = -1.0f + touchY / 2000.0f;

    float centerX = -1.0f + touchX / 2000.0f;
    float centerY = -1.0f + touchY / 2000.0f;
    float radius = 1.0f;
    tX = centerX + radius * cosf(angle);
    tY = centerY + radius * sinf(angle);
    angle += 0.05f; // speed

    tX = tX * 427 + 427;
    tY = tY * 240 + 240;

    testRect.coords = (Rect){
        tX + 300, tX + 600, tY + 150, tY + 450
    };

    // Render!
    WHBGfxBeginRender();

    WHBGfxBeginRenderTV();
    WHBGfxClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    GX2SetFetchShader(&group.fetchShader);
    GX2SetVertexShader(group.vertexShader);
    GX2SetPixelShader(group.pixelShader);
    // GX2RSetAttributeBuffer(&positionBuffer, 0, positionBuffer.elemSize, 0);
    // GX2RSetAttributeBuffer(&colourBuffer, 1, colourBuffer.elemSize, 0);
    // GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 3, 0, 1);
    drawRect(testRect.coords, testRect.color, &testRect.rBuffer, &testRect.cBuffer, &group);
    WHBGfxFinishRenderTV();

    WHBGfxBeginRenderDRC();
    WHBGfxClearColor(1.0f, 0.0f, 1.0f, 1.0f);
    GX2SetFetchShader(&group.fetchShader);
    GX2SetVertexShader(group.vertexShader);
    GX2SetPixelShader(group.pixelShader);
    // GX2RSetAttributeBuffer(&positionBuffer, 0, positionBuffer.elemSize, 0);
    // GX2RSetAttributeBuffer(&colourBuffer, 1, colourBuffer.elemSize, 0);
    // GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 3, 0, 1);
    drawRect(testRect.coords, testRect.color, &testRect.rBuffer, &testRect.cBuffer, &group);
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
