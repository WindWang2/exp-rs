import numpy as np

def calculate_polynomial_coeffs(src_pts, dst_pts, order=1):
    num_pts = src_pts.shape[0]
    if order == 1:
        A = np.column_stack([np.ones(num_pts), src_pts[:, 0], src_pts[:, 1]])
    else:
        A = np.column_stack([
            np.ones(num_pts), src_pts[:, 0], src_pts[:, 1],
            src_pts[:, 0]**2, src_pts[:, 0]*src_pts[:, 1], src_pts[:, 1]**2
        ])
    
    coeffs_x, _, _, _ = np.linalg.lstsq(A, dst_pts[:, 0], rcond=None)
    coeffs_y, _, _, _ = np.linalg.lstsq(A, dst_pts[:, 1], rcond=None)
    return coeffs_x, coeffs_y
