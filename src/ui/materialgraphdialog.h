#ifndef MATERIALGRAPHDIALOG_H
#define MATERIALGRAPHDIALOG_H
#include <array>
#include <functional>
#include <vector>
#include <QString>
#include <QStringList>
#include <QByteArray>
class NifModel;
class QModelIndex;
class QWidget;
class QMainWindow;
class QDialog;
struct MaterialGraphPreviewState {
 double time=0;
 std::array<float,2> scale{},offset{};
 std::array<float,4> color{};
 std::array<float,3> specular{1,1,1};
 float brightness=1,alpha=1;
 unsigned type=0,flags1=0,flags2=0;
 QStringList paths;
 std::vector<std::pair<QString,float>> parameters;
};
struct MaterialGraphHooks {
 QWidget *placement=nullptr;
 bool allowOverlay=false;
 QString overlayReason;
 std::function<QString()> validate;
 std::function<QString(bool)> finish;
 std::function<void(const MaterialGraphPreviewState &)> preview;
};
QDialog *createMaterialGraph(NifModel *nif,const QModelIndex &selection,QWidget *parent,MaterialGraphHooks hooks={});
void showMaterialGraphWorkspace(NifModel *nif,const QModelIndex &selection,QMainWindow *parent);
QString commitMaterialGraphDocument(NifModel *nif,const QByteArray &before,const QByteArray &candidate,const QString &label);
void showMaterialGraph(NifModel *nif, const QModelIndex &selection, QWidget *parent);
#endif
