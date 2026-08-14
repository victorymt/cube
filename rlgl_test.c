#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <stdio.h>
/* rlMatrixMultiply is static in rlgl.h — replicate its exact code here */
static Matrix RlMul(Matrix left, Matrix right){
    Matrix result = { 0 };
    result.m0 = left.m0*right.m0 + left.m1*right.m4 + left.m2*right.m8 + left.m3*right.m12;
    result.m1 = left.m0*right.m1 + left.m1*right.m5 + left.m2*right.m9 + left.m3*right.m13;
    result.m2 = left.m0*right.m2 + left.m1*right.m6 + left.m2*right.m10 + left.m3*right.m14;
    result.m3 = left.m0*right.m3 + left.m1*right.m7 + left.m2*right.m11 + left.m3*right.m15;
    result.m4 = left.m4*right.m0 + left.m5*right.m4 + left.m6*right.m8 + left.m7*right.m12;
    result.m5 = left.m4*right.m1 + left.m5*right.m5 + left.m6*right.m9 + left.m7*right.m13;
    result.m6 = left.m4*right.m2 + left.m5*right.m6 + left.m6*right.m10 + left.m7*right.m14;
    result.m7 = left.m4*right.m3 + left.m5*right.m7 + left.m6*right.m11 + left.m7*right.m15;
    result.m8 = left.m8*right.m0 + left.m9*right.m4 + left.m10*right.m8 + left.m11*right.m12;
    result.m9 = left.m8*right.m1 + left.m9*right.m5 + left.m10*right.m9 + left.m11*right.m13;
    result.m10 = left.m8*right.m2 + left.m9*right.m6 + left.m10*right.m10 + left.m11*right.m14;
    result.m11 = left.m8*right.m3 + left.m9*right.m7 + left.m10*right.m11 + left.m11*right.m15;
    result.m12 = left.m12*right.m0 + left.m13*right.m4 + left.m14*right.m8 + left.m15*right.m12;
    result.m13 = left.m12*right.m1 + left.m13*right.m5 + left.m14*right.m9 + left.m15*right.m13;
    result.m14 = left.m12*right.m2 + left.m13*right.m6 + left.m14*right.m10 + left.m15*right.m14;
    result.m15 = left.m12*right.m3 + left.m13*right.m7 + left.m14*right.m11 + left.m15*right.m15;
    return result;
}
static Vector4 Apply(Matrix M, Vector4 v){
    Vector4 r;
    r.x = M.m0*v.x + M.m4*v.y + M.m8*v.z + M.m12*v.w;
    r.y = M.m1*v.x + M.m5*v.y + M.m9*v.z + M.m13*v.w;
    r.z = M.m2*v.x + M.m6*v.y + M.m10*v.z + M.m14*v.w;
    r.w = M.m3*v.x + M.m7*v.y + M.m11*v.z + M.m15*v.w;
    return r;
}
int main(void){
    Vector3 pos={10,20,30}, target={10,20,34}, up={0,1,0};
    Camera3D cam = { .position=pos, .target=target, .up=up, .fovy=128, .projection=CAMERA_ORTHOGRAPHIC };
    Matrix view = GetCameraMatrix(cam);
    Matrix proj = MatrixOrtho(-64,64,-64,64,0.05f,4000.0f);
    Vector4 w2 = {20,20,34,1};
    /* rlgl render pass: matMVP = rlMatrixMultiply(modelview, projection) */
    Matrix rlglMVP = RlMul(view, proj);
    /* game: lightMatrix = MatrixMultiply(view, proj) */
    Matrix gameLM = MatrixMultiply(view, proj);
    Vector4 a = Apply(rlglMVP, w2);
    Vector4 b = Apply(gameLM, w2);
    printf("rlgl MVP (render pass) : %f %f %f %f\n", a.x,a.y,a.z,a.w);
    printf("game lightMatrix       : %f %f %f %f\n", b.x,b.y,b.z,b.w);
    printf("match: %s\n", (a.x==b.x && a.y==b.y && a.z==b.z) ? "YES" : "NO");
    return 0;
}
