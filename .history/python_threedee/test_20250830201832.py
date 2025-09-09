import numpy as np

# Extracted c2w matrix from the new JSON file
c2w_new = np.array([
    [0.23052924871444702, -0.9699694514274597, -0.07756005227565765],
    [0.9730654358863831, 0.22979581356048584, 0.018374769017100334],
    [7.450580596923828e-09, -0.07970692217350006, 0.9968183636665344]
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
if check_orthogonality(c2w_new):
    print("The c2w matrix is orthogonal.")
else:
    print("The c2w matrix is not orthogonal.")
