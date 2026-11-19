// The other former weak stubs have real strong definitions. Keep this sole
// fallback strong as well: adding the real implementation must fail at link
// time rather than silently selecting an ABI-incompatible substitute.

#if defined(__GNUC__) || defined(__clang__)

// QgsGradientPlotEventFilter::eventFilter(QObject*, QEvent*)
_Bool _ZN26QgsGradientPlotEventFilter11eventFilterEP7QObjectP6QEvent( void *, void * ) __attribute__((visibility("default")));
_Bool _ZN26QgsGradientPlotEventFilter11eventFilterEP7QObjectP6QEvent( void *, void * ) { return 0; }

#endif
