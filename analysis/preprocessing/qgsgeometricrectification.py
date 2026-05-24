import numpy as np
from analysis.qgsprocessingregistry import register_tool

@register_tool(
    name="polynomial_rectify",
    label="Polynomial Rectification",
    category="Preprocessing",
    description="Calculate polynomial coefficients for geometric rectification",
    params=[
        {"name": "src_pts", "label": "Source Points", "type": "array", "required": True, "help": "Array of source points"},
        {"name": "dst_pts", "label": "Destination Points", "type": "array", "required": True, "help": "Array of destination points"},
        {"name": "order", "label": "Polynomial Order", "type": "int", "default": 1, "help": "Order of the polynomial transformation"}
    ]
)
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
