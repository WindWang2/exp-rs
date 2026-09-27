Test project /home/kevin/project/exp-rs-qt-teardown-r4/build-r4
      Start  396: LayoutDesigner: QgsLayoutDesignerDialog lifecycle and item creation
 1/22 Test  #396: LayoutDesigner: QgsLayoutDesignerDialog lifecycle and item creation .....   Passed    0.74 sec
      Start  596: Project lifecycle churn: clear/import/view cycles stay consistent
 2/22 Test  #596: Project lifecycle churn: clear/import/view cycles stay consistent .......   Passed    1.38 sec
      Start  597: Project lifecycle churn: canvas destroyed before clearProject
 3/22 Test  #597: Project lifecycle churn: canvas destroyed before clearProject ...........   Passed    1.24 sec
      Start 1026: Canvas teardown: used canvas with CRS extent deletes cleanly
 4/22 Test #1026: Canvas teardown: used canvas with CRS extent deletes cleanly ............   Passed    0.63 sec
      Start 1027: Canvas teardown: delete while refresh scheduled does not crash
 5/22 Test #1027: Canvas teardown: delete while refresh scheduled does not crash ..........   Passed    0.65 sec
      Start 1028: Canvas teardown: peer deleted first leaves survivor valid
 6/22 Test #1028: Canvas teardown: peer deleted first leaves survivor valid ...............   Passed    0.65 sec
      Start 1029: Canvas teardown: extent churn then immediate delete is safe
 7/22 Test #1029: Canvas teardown: extent churn then immediate delete is safe .............   Passed    0.72 sec
      Start 1030: Canvas teardown: CRS switch churn then delete is safe
 8/22 Test #1030: Canvas teardown: CRS switch churn then delete is safe ...................   Passed    0.63 sec
      Start 1031: Dual viewport teardown: canvases die before controller
 9/22 Test #1031: Dual viewport teardown: canvases die before controller ..................   Passed    0.67 sec
      Start 1032: Dual viewport teardown: controller dies before canvases
10/22 Test #1032: Dual viewport teardown: controller dies before canvases .................   Passed    0.63 sec
      Start 1033: Dual viewport teardown: rapid extent churn then delete-all
11/22 Test #1033: Dual viewport teardown: rapid extent churn then delete-all ..............   Passed    0.69 sec
      Start 1034: Dual viewport teardown: sequential controllers over one canvas pair
12/22 Test #1034: Dual viewport teardown: sequential controllers over one canvas pair .....   Passed    0.74 sec
      Start 1035: Timeline scrubber teardown: destroyed while playing stops ticks
13/22 Test #1035: Timeline scrubber teardown: destroyed while playing stops ticks .........   Passed    0.47 sec
      Start 1036: Timeline scrubber teardown: pause-then-delete ends state machine
14/22 Test #1036: Timeline scrubber teardown: pause-then-delete ends state machine ........   Passed    0.43 sec
      Start 1037: Timeline scrubber teardown: playback reaches final slice and finishes
15/22 Test #1037: Timeline scrubber teardown: playback reaches final slice and finishes ...   Passed    2.37 sec
      Start 1038: ROI statistics teardown: compute-then-destroy keeps pool clean
16/22 Test #1038: ROI statistics teardown: compute-then-destroy keeps pool clean ..........   Passed    0.55 sec
      Start 1039: ROI statistics teardown: destroy mid-compute cancels generation
17/22 Test #1039: ROI statistics teardown: destroy mid-compute cancels generation .........   Passed    0.68 sec
      Start 1040: ROI statistics teardown: repeated widgets over one pool stay clean
18/22 Test #1040: ROI statistics teardown: repeated widgets over one pool stay clean ......   Passed    0.97 sec
      Start 1041: Map tool teardown: canvas destroyed with active parented tool
19/22 Test #1041: Map tool teardown: canvas destroyed with active parented tool ...........   Passed    0.63 sec
      Start 1042: Map tool teardown: unsetMapTool then canvas destruction retires both
20/22 Test #1042: Map tool teardown: unsetMapTool then canvas destruction retires both ....   Passed    0.59 sec
      Start 1043: Map tool teardown: tool switch churn then delete-all
21/22 Test #1043: Map tool teardown: tool switch churn then delete-all ....................   Passed    0.56 sec
      Start 1049: pi_bridge_lifecycle
22/22 Test #1049: pi_bridge_lifecycle .....................................................   Passed    8.45 sec

100% tests passed, 0 tests failed out of 22

Total Test time (real) =  27.37 sec
