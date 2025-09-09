using GTA;
using GTA.Math;
using System;
using System.Runtime.InteropServices;

public class CamInfoBufferSigned : Script
{
    // 17 个 double：1 触发头 + 1 counter + 13 payload + 2 hash
    private static double[] buf = new double[17];
    private static GCHandle h;
    private double counter = 1.0;   // > 0.5

    // 触发头：任意稳定常量即可（用于扫描起点占位）
    private const double TRIGGER = 1.23456789012345e+100;

    public CamInfoBufferSigned()
    {
        this.Interval = 0;
        this.Tick += OnTick;
        this.Aborted += OnAbort;

        h = GCHandle.Alloc(buf, GCHandleType.Pinned);
        buf[0] = TRIGGER; // 触发头
    }

    private void OnAbort(object sender, EventArgs e)
    {
        try { if (h.IsAllocated) h.Free(); } catch {}
    }

    private static void RowMajorCam2World(Matrix R, Vector3 C, double[] dst, int off)
    {
        // 行主序 3×4（R|C）：每行写 4 个 double
        dst[off + 0] = R.M11; dst[off + 1] = R.M12; dst[off + 2] = R.M13; dst[off + 3] = C.X;
        dst[off + 4] = R.M21; dst[off + 5] = R.M22; dst[off + 6] = R.M23; dst[off + 7] = C.Y;
        dst[off + 8] = R.M31; dst[off + 9] = R.M32; dst[off +10] = R.M33; dst[off +11] = C.Z;
    }

    private void OnTick(object sender, EventArgs e)
    {
        try
        {
            // 读相机
            Vector3 C = GameplayCamera.Position;
            Vector3 rotDeg = GameplayCamera.Rotation; // X=pitch, Y=roll, Z=yaw（度）
            float fovDeg = GameplayCamera.FieldOfView;

            // 欧拉角（度→弧度），R = Rz(yaw) * Ry(roll) * Rx(pitch)
            double rx = rotDeg.X * Math.PI / 180.0;
            double ry = rotDeg.Y * Math.PI / 180.0;
            double rz = rotDeg.Z * Math.PI / 180.0;

            Matrix RxM = Matrix.RotationX((float)rx);
            Matrix RyM = Matrix.RotationY((float)ry);
            Matrix RzM = Matrix.RotationZ((float)rz);
            Matrix R = RzM * RyM * RxM;

            // 写 counter（每帧自增一点，确保 > 0.5）
            counter = counter + 1.0;
            if (counter < 1.0) counter = 1.0;
            buf[1] = counter;

            // 写 12 个外参（行主序）到 [2..13] 的前 12 格
            RowMajorCam2World(R, C, buf, 2);

            // FOV 放在 [14]
            buf[14] = (double)fovDeg;

            // 计算校验
            // allsum = counter + sum(payload 13 个)
            double allsum = buf[1];
            double plusminus = buf[1];
            // payload 索引相对于 payload 的 i=0..12 —— 实际在 buf 的 [2..14]
            for (int i = 0; i < 13; ++i)
            {
                double v = buf[2 + i];
                allsum += v;
                if ((i + 1) % 2 == 0) // 模板里 ii 从 1 开始：偶数加、奇数减
                    plusminus += v;
                else
                    plusminus -= v;
            }
            buf[15] = allsum;
            buf[16] = plusminus;
        }
        catch { }
    }
}
