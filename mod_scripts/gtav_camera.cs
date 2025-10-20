using GTA;
using GTA.Math;
using System;
using System.Runtime.InteropServices;

public class CamInfoBufferSigned : Script
{
    private static double[] buf = new double[17];
    private static GCHandle h;
    private double counter = 1.0;   
    private const double TRIGGER = 1.38097189588312856e-12;
    public CamInfoBufferSigned()
    {
        this.Interval = 0;
        this.Tick += OnTick;
        this.Aborted += OnAbort;

        h = GCHandle.Alloc(buf, GCHandleType.Pinned);
        buf[0] = TRIGGER; 
    }

    private void OnAbort(object sender, EventArgs e)
    {
        try { if (h.IsAllocated) h.Free(); } catch {}
    }
    private static void ColumnMajorCam2World(Matrix R, Vector3 C, double[] dst, int off)
    {
        // 转置行主序矩阵 R 为列主序
        dst[off + 0] = R.M11; dst[off + 1] = R.M31; dst[off + 2] = -R.M21; dst[off + 3] = C.X;
        dst[off + 4] = R.M12; dst[off + 5] = R.M32; dst[off + 6] = -R.M22; dst[off + 7] = C.Y;
        dst[off + 8] = R.M13; dst[off + 9] = R.M33; dst[off +10] = -R.M23; dst[off +11] = C.Z;
    }


    private void OnTick(object sender, EventArgs e)
    {
        try
        {
            Vector3 C = GameplayCamera.Position;
            Vector3 rotDeg = GameplayCamera.Rotation;
            float fovDeg = GameplayCamera.FieldOfView;

            float rx = rotDeg.X * (float)(Math.PI / 180.0);
            float ry = rotDeg.Y * (float)(Math.PI / 180.0);
            float rz = rotDeg.Z * (float)(Math.PI / 180.0);

            // 注意顺序：Pitch -> Roll -> Yaw
            Matrix R = Matrix.RotationX(rx) * Matrix.RotationY(ry) * Matrix.RotationZ(rz);

            // 左手系转右手系（如果需要）
            R.M13 = -R.M13; R.M23 = -R.M23; R.M33 = -R.M33;
            C.Z = -C.Z;

            // OpenCV -> Open3D：Y 轴取反
            R.M12 = -R.M12; R.M22 = -R.M22; R.M32 = -R.M32;
            C.Y = -C.Y;
            C *= 3.0f;
            // // 存储列主序 c2w
            // ColumnMajorCam2World(R, C, buf, 2);
            // buf[14] = (double)fovDeg;


            counter = counter + 1.0;
            if (counter < 1.0) counter = 1.0;
            buf[1] = counter;

            // 使用列主序格式存储矩阵
            ColumnMajorCam2World(R, C, buf, 2);

            // 存储 FOV
            buf[14] = (double)fovDeg;

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
        catch { }
    }

}
