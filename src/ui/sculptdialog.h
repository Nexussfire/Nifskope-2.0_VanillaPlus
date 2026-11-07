#ifndef SCULPTDIALOG_H
#define SCULPTDIALOG_H
#include "geometry/vanillaplus.h"
#include <QString>
#include <functional>
class NifModel;
class QWidget;

// The model is private and outlives the modal workspace. commit stages and
// validates against the live document; preview writes never reach that model.
void showSculptWorkspace(NifModel *preview,int shapeBlock,int dataBlock,bool classic,
	const VanillaPlus::Mesh &mesh,std::function<QString(const VanillaPlus::Mesh &,bool)> commit,QWidget *parent,const QString &notice={});
#endif
