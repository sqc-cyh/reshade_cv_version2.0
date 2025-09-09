import numpy as np

# Extracted c2w matrix from the provided JSON file
c2w = np.array([
    [-0.6396541595458984, 0.7686310410499573, 0.006987080909311771],
    [-0.7137196063995361, -0.5972827076911926, 0.3658655285835266],
    [0.28538888692855835, 0.22904060781002045, 0.9306414127349854]
])

# Check orthogonality by verifying R^T * R = I
def check_orthogonality(matrix):
    # Calculate the transpose of the matrix
    matrix_T = matrix.T
    # Calculate R^T * R
    product = np.dot(matrix_T, matrix)
    # Identity matrix of the same dimension
    identity_matrix = np.identity(matrix.shape[0])
    # Check if the product is close to the identity matrix
    return np.allclose(product, identity_matrix)

# Run the check
if check_orthogonality(c2w):
    print("The c2w matrix is orthogonal.")
else:
    print("The c2w matrix is not orthogonal.")
