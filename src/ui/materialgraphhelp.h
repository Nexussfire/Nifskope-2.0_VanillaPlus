#ifndef MATERIALGRAPHHELP_H
#define MATERIALGRAPHHELP_H
class QDialog;
class QGraphicsView;
// Scoped, non-activating help inside this graph (and its owned modal dialogs).
// Does not change application-wide tooltip timing or the ordinary editor.
void installMaterialGraphHelp(QDialog *dialog,QGraphicsView *view);
#endif
