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
    private static void ColMajorCam2World(Matrix R, Vector3 C, double[] dst, int off)
    {
        // 第一列：X轴
        dst[off + 0] = R.M11;
        dst[off + 1] = R.M12;
        dst[off + 2] = R.M13;
        dst[off + 3] = C.X;
        // 第二列：Y轴
        dst[off + 4] = R.M21;
        dst[off + 5] = R.M22;
        dst[off + 6] = R.M23;
        dst[off + 7] = C.Y;
        // 第三列：Z轴
        dst[off + 8]  = R.M31;
        dst[off + 9]  = R.M32;
        dst[off + 10] = R.M33;
        dst[off + 11] = C.Z;
    }

    private void OnTick(object sender, EventArgs e)
    {
        try
        {
   
            Vector3 C = GameplayCamera.Position;
            Vector3 rotDeg = GameplayCamera.Rotation;
            float fovDeg = GameplayCamera.FieldOfView;

            double rx = rotDeg.X * Math.PI / 180.0;
            double ry = rotDeg.Y * Math.PI / 180.0;
            double rz = rotDeg.Z * Math.PI / 180.0;

            Matrix RxM = Matrix.RotationX((float)rx);
            Matrix RyM = Matrix.RotationY((float)ry);
            Matrix RzM = Matrix.RotationZ((float)rz);
            Matrix R = RzM * RyM * RxM;

            counter = counter + 1.0;
            if (counter < 1.0) counter = 1.0;
            buf[1] = counter;

            RowMajorCam2World(R, C, buf, 2);

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
