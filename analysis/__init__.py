from analysis.qgsprocessingregistry import QgsProcessingRegistry, ToolRegistry, register_tool

# Import all tools to trigger their @register_tool decorators
from analysis.preprocessing.qgsatmospherictreatment import calculate_dos1
from analysis.preprocessing.qgsgeometricrectification import calculate_polynomial_coeffs
from analysis.processing.qgsindices import calculate_ndvi, calculate_ndwi
from analysis.processing.qgspansharpening import pca_pansharpen_arrays
from analysis.processing.qgsclassification import kmeans_classify

__all__ = [
    'QgsProcessingRegistry',
    'ToolRegistry',
    'register_tool',
    'calculate_dos1',
    'calculate_polynomial_coeffs',
    'calculate_ndvi',
    'calculate_ndwi',
    'pca_pansharpen_arrays',
    'kmeans_classify',
]
