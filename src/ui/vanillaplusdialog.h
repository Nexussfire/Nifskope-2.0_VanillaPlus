#ifndef VANILLAPLUSDIALOG_H
#define VANILLAPLUSDIALOG_H

class NifModel;
class QModelIndex;
class QWidget;

// Preview and explicitly apply a staged shape to the open model; never saves files.
void showVanillaPlusGeometry( NifModel * nif, const QModelIndex & selection, QWidget * parent );
void showSculptGeometry( NifModel * nif, const QModelIndex & selection, QWidget * parent );

#endif
