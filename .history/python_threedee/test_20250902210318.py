from nerfvis import scene
import numpy as np

density = 1.0 / (np.linalg.norm((np.mgrid[:100, :100, :100].transpose(1, 2, 3, 0) - 45.5) / 50,
                         axis=-1) + 1e-5)  # (Dx, Dy, Dz)
color = np.zeros((100, 100, 100, 3), dtype=np.float32) # (Dx, Dy, Dz, 3)
color[..., 0] = 1.0
color[..., 1] = 0.5
scene.add_volume('My volume 1', density, color, scale=0.2, translation=[-1, 0, 0])

color[..., 1] = 0.0
scene.add_volume('My volume 2', density, color, scale=0.2, translation=[1, 0, 0])
scene.display() # or embed(), etc