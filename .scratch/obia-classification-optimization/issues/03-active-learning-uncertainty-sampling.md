# 03 — Active Learning & Uncertainty Sampling Guided Annotation

Type: grilling
Status: resolved
Blocked by: 01

## Question

Manual ROI labeling of hundreds of segments is labor-intensive. Classifier prediction probability distributions yield uncertainty metrics (entropy $H = -\sum p_i \log p_i$ or margin $1 - (p_{\text{max1}} - p_{\text{max2}})$) that pinpoint the most ambiguous segments.

How should `RsObjectClassify` compute and output per-segment uncertainty scores, and how should `RsObiaMainWindow` visualize high-uncertainty candidate segments to guide active learning and targeted ROI annotation?
