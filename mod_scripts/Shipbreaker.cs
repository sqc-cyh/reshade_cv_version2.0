using BepInEx;
using UnityEngine;
using System;
using System.Runtime.InteropServices;

[BepInPlugin("shipbreaker", "CamInfoBufferSigned","1.0.0")]
public class CamInfoBufferSigned : BaseUnityPlugin
{
    private static double[] buf = new double[17];
    private static GCHandle h;
    private double counter = 1.0;
    private const double TRIGGER = 1.38097189588312856e-12;

    //public static void Main()
    //{
    //    Debug.Log("Hello, world!");

    //    // 创建 GameObject 并添加此组件
    //    GameObject go = new GameObject("CamInfoBuffer");
    //    CamInfoBufferSigned instance = go.AddComponent<CamInfoBufferSigned>();

    //    // 确保 GameObject 在场景切换时不被销毁
    //    DontDestroyOnLoad(go);
    //}

    void Awake()
    {
        Debug.Log("CamInfoBufferSigned Awake called!");
        h = GCHandle.Alloc(buf, GCHandleType.Pinned);
        buf[0] = TRIGGER;
    }

    void OnDestroy()
    {
        try
        {
            if (h.IsAllocated)
                h.Free();
            Debug.Log("CamInfoBufferSigned OnDestroy called!");
        }
        catch (Exception e)
        {
            Debug.LogError($"Error in OnDestroy: {e.Message}");
        }
    }

    void Update()
    {
        try
        {
            //获取主相机
            Camera cam = Camera.main;
            if (cam == null) return;

            Vector3 C = cam.transform.position;
            Quaternion rotation = cam.transform.rotation;
            float fovDeg = cam.fieldOfView;

            //将四元数转换为旋转矩阵
            Matrix4x4 R = Matrix4x4.Rotate(rotation);

            if (UnityEngine.Input.GetKeyDown(KeyCode.F11))
            {
                // 转换为欧拉角（度数制）
                Vector3 euler = rotation.eulerAngles;
                Logger.LogInfo($"[F11 Triggered] Euler Angles (Pitch, Yaw, Roll): {euler}");
            }

            //更新计数器
            counter = counter + 1.0;
            if (counter < 1.0) counter = 1.0;
            buf[1] = counter;

            //存储列主序矩阵
            ColumnMajorCam2World(R, C, buf, 2);

            //存储FOV
            buf[14] = (double)fovDeg;

            //计算哈希值
            double allsum = buf[1];
            double plusminus = buf[1];
            for (int i = 0; i < 13; ++i)
            {
                double v = buf[2 + i];
                allsum += v;
                if ((i + 1) % 2 == 0)
                    plusminus += v;
                else
                    plusminus -= v;
            }
            buf[15] = allsum;
            buf[16] = plusminus;
        }
        catch (Exception e)
        {
            Debug.LogError($"Error in Update: {e.Message}");
        }
    }

    private static void ColumnMajorCam2World(Matrix4x4 R, Vector3 C, double[] dst, int off)
    {
        dst[off + 0] = R.m00; dst[off + 1] = -R.m01; dst[off + 2] = -R.m02; dst[off + 3] = C.x;
        dst[off + 4] = R.m10; dst[off + 5] = -R.m11; dst[off + 6] = -R.m12; dst[off + 7] = C.y;
        dst[off + 8] = R.m20; dst[off + 9] = -R.m21; dst[off + 10] = -R.m22; dst[off + 11] = C.z;
    }
}