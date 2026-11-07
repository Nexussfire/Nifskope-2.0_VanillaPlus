#include "model/nifmodel.h"
#include "ui/materialgraphdialog.h"
#include "gl/glproperty.h"
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char *message) { if (!ok) throw std::runtime_error(message); }
QByteArray bytes(const NifModel &model) { QByteArray data; QBuffer out(&data); out.open(QIODevice::WriteOnly); require(model.save(out),"save failed"); return data; }
template<class T> T *control(QDialog *dialog,const char *name) {
	if (auto child=dialog->findChild<T *>(name)) return child;
	auto view=dialog->findChild<QGraphicsView *>("materialGraphView"); if (!view) return nullptr;
	for (auto item:view->scene()->items()) if (auto proxy=qgraphicsitem_cast<QGraphicsProxyWidget *>(item)) if (auto child=proxy->widget()->findChild<T *>(name)) return child;
	return nullptr;
}
}
int main(int argc,char **argv)
{
	QApplication app(argc,argv); app.setQuitOnLastWindowClosed(false);
	QTemporaryDir settings; QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
	app.setOrganizationName("MaterialShaderProbe"); app.setApplicationName("MaterialShaderProbe");
	try {
		require(argc==3 && NifModel::loadXML(),"expected NIF and texture root"); NifModel source; require(source.loadFromFile(QString::fromLocal8Bit(argv[1])),"load failed");
		int shader=-1; for (int i=0;i<source.getBlockCount();++i) if (source.isNiBlock(source.getBlockIndex(i),"BSLightingShaderProperty")) { shader=i; break; }
		require(shader>=0,"no lighting shader"); auto sourceBytes=bytes(source);
		for (unsigned type:{unsigned(ShaderFlags::ST_Default),unsigned(ShaderFlags::ST_GlowShader),unsigned(ShaderFlags::ST_EnvironmentMap),unsigned(ShaderFlags::ST_Heightmap)}) {
			for (int mode=0;mode<3;++mode) { // Cancel, Apply, invalid input
				NifModel nif; QBuffer input(&sourceBytes); input.open(QIODevice::ReadOnly); require(nif.load(input),"copy failed");
				nif.getGameResources().dataPaths.prepend(QString::fromLocal8Bit(argv[2])); QUndoStack undo; nif.undoStack=&undo;
				auto index=nif.getBlockIndex(shader); unsigned flags1=nif.get<unsigned>(index,"Shader Flags 1"),flags2=nif.get<unsigned>(index,"Shader Flags 2");
				bool done=false; QString failure;
				QTimer::singleShot(0,[&]() {
					auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
					try {
						auto choice=control<QComboBox>(dialog,"materialShaderType"); auto flagButton=control<QPushButton>(dialog,"materialShaderFlags"); require(choice && flagButton,"missing shader controls");
						require(!choice->toolTip().isEmpty(),"shader tooltip missing"); choice->setCurrentIndex(choice->findData(type));
						bool flagsEdited=false;
						QTimer::singleShot(0,[&]() {
							auto flags=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!flags || flags->objectName()!="materialShaderFlagsDialog") return;
							auto spec=flags->findChild<QCheckBox *>("materialFlagSpecular"),glow=flags->findChild<QCheckBox *>("materialFlagGlow"),env=flags->findChild<QCheckBox *>("materialFlagEnvironment"),parallax=flags->findChild<QCheckBox *>("materialFlagParallax"),emission=flags->findChild<QCheckBox *>("materialFlagEmission");
							if (!spec || !glow || !env || !parallax || !emission) { flags->reject(); return; }
							if (type==ShaderFlags::ST_GlowShader && (!glow->isChecked() || !emission->isChecked())) { flags->reject(); return; }
							if (type==ShaderFlags::ST_EnvironmentMap && !env->isChecked()) { flags->reject(); return; }
							if (type==ShaderFlags::ST_Heightmap && !parallax->isChecked()) { flags->reject(); return; }
							spec->setChecked(!(flags1&ShaderFlags::SLSF1_Specular)); flagsEdited=!spec->toolTip().isEmpty(); flags->accept();
						});
						flagButton->click(); require(flagsEdited,"flag presets/tooltips failed");
						auto slot=control<QLabel>(dialog,type==ShaderFlags::ST_EnvironmentMap?"materialTextureStatus4":type==ShaderFlags::ST_Heightmap?"materialTextureStatus3":"materialTextureStatus2");
						if (type!=ShaderFlags::ST_Default) require(slot && slot->text().startsWith("Feature enabled"),"feature state not refreshed");
						auto color=control<QLineEdit>(dialog,"materialEmissiveColor"); require(color && !color->toolTip().isEmpty(),"emission control missing");
						color->setText(mode==2?"nan 1 1":"1 0.5 0.25");
						if (type==ShaderFlags::ST_EnvironmentMap) if (auto env=control<QLineEdit>(dialog,"materialEnvironmentMapScale")) env->setText("2.5");
						if (mode==1 && type==ShaderFlags::ST_EnvironmentMap) dialog->grab().save("release/materialgraph-shader-polish.png");
						require(bytes(nif)==sourceBytes && undo.count()==0,"staged controls changed live model");
						if (mode==0) dialog->reject(); else {
							control<QPushButton>(dialog,"materialGraphApply")->click();
							if (mode==2) { require(dialog->result()!=QDialog::Accepted && bytes(nif)==sourceBytes && undo.count()==0,"invalid color was not atomic"); dialog->reject(); }
							else require(dialog->result()==QDialog::Accepted,qPrintable(control<QLabel>(dialog,"materialGraphStatus")->text()));
						}
						done=true;
					} catch(const std::exception &e) { failure=QString::fromUtf8(e.what()); dialog->reject(); }
				});
				showMaterialGraph(&nif,index,nullptr); require(done,qPrintable(failure));
				if (mode!=1) { require(bytes(nif)==sourceBytes,"Cancel changed source"); continue; }
				require(undo.count()==1,"Apply was not one Undo action"); auto edited=bytes(nif);
				NifModel reopened; QBuffer verify(&edited); verify.open(QIODevice::ReadOnly); require(reopened.load(verify),"shader change failed save/reopen");
				auto checked=reopened.getBlockIndex(shader); require(reopened.get<unsigned>(checked,"Shader Type")==type,"wrong shader type on reopen");
				auto emission=reopened.get<Color3>(checked,"Emissive Color"); require(emission[0]==1 && emission[1]==.5f && emission[2]==.25f,"emission RGB failed save/reopen");
				constexpr unsigned managed1=ShaderFlags::SLSF1_Specular|ShaderFlags::SLSF1_Own_Emit|ShaderFlags::SLSF1_Environment_Mapping|ShaderFlags::SLSF1_Parallax;
				require((reopened.get<unsigned>(checked,"Shader Flags 1")&~managed1)==(flags1&~managed1),"unexposed flags 1 changed");
				require((reopened.get<unsigned>(checked,"Shader Flags 2")&~unsigned(ShaderFlags::SLSF2_Glow_Map))==(flags2&~unsigned(ShaderFlags::SLSF2_Glow_Map)),"unexposed flags 2 changed");
				if (type==ShaderFlags::ST_EnvironmentMap) {
					require(reopened.get<float>(checked,"Environment Map Scale")==2.5f,"conditional environment strength failed");
				}
				for (int i=0;i<source.getBlockCount();++i) if (i!=shader) {
					QByteArray a,b; QBuffer ba(&a),bb(&b); ba.open(QIODevice::WriteOnly); bb.open(QIODevice::WriteOnly);
					require(source.saveIndex(ba,source.getBlockIndex(i)) && reopened.saveIndex(bb,reopened.getBlockIndex(i)) && a==b,"unrelated block changed");
				}
				undo.undo(); require(bytes(nif)==sourceBytes,"shader Undo not exact"); undo.redo(); require(bytes(nif)==edited,"shader Redo not exact");
				if (type==ShaderFlags::ST_EnvironmentMap) {
					bool changed=false; QTimer::singleShot(0,[&]() {
						auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!d) return;
						auto choice=control<QComboBox>(d,"materialShaderType"); choice->setCurrentIndex(choice->findData(unsigned(ShaderFlags::ST_GlowShader)));
						control<QPushButton>(d,"materialGraphApply")->click(); changed=d->result()==QDialog::Accepted; if (!changed) d->reject();
					});
					showMaterialGraph(&nif,index,nullptr); require(changed,"Environment to Glow failed");
					undo.undo(); require(bytes(nif)==edited && nif.get<float>(index,"Environment Map Scale")==2.5f,"Undo lost existing conditional environment data");
				}
			}
			std::cout << "BS " << source.getBSVersion() << " shader " << type << ": staged flags, labels, tooltip, Apply/reopen, Cancel, invalid-color rejection, exact Undo/Redo and unrelated blocks passed\n";
		}
		// Exercise the full lists, including both high bits, without involving
		// shader presets. First reject the inner dialog, then accept all bits.
		for (bool applyAll:{false,true}) {
			NifModel nif; QBuffer input(&sourceBytes); input.open(QIODevice::ReadOnly); require(nif.load(input),"full-list copy failed");
			QUndoStack undo; nif.undoStack=&undo; auto index=nif.getBlockIndex(shader);
			quint32 original[2]={nif.get<quint32>(index,"Shader Flags 1"),nif.get<quint32>(index,"Shader Flags 2")};
			bool done=false; QString failure;
			QTimer::singleShot(0,[&]() {
				auto graph=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!graph) return;
				try {
					bool inspected=false;
					QTimer::singleShot(0,[&]() {
						auto flags=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!flags) return;
						quint32 seen[2]={0,0}; bool valid=true; int count=0;
						for (auto box:flags->findChildren<QCheckBox *>()) {
							int word=box->property("flagWord").toInt()-1,bit=box->property("flagBit").toInt();
							if (word<0 || word>1 || bit<0 || bit>31) { valid=false; continue; }
							quint32 mask=quint32(1)<<bit; valid &= !(seen[word]&mask); seen[word]|=mask; ++count;
							valid &= box->isChecked()==bool(original[word]&mask) && box->isEnabled() && !box->toolTip().isEmpty();
							box->setChecked(!box->isChecked());
						}
						auto words=flags->findChild<QLabel *>("materialFlagWords");
						valid &= words && words->text().contains(QString::number(~original[0],16)) && words->text().contains(QString::number(~original[1],16));
						inspected=valid && count==64 && seen[0]==0xffffffffu && seen[1]==0xffffffffu;
						if (source.getBSVersion()==100) flags->grab().save("release/materialgraph-full-flags.png");
						if (applyAll && inspected) flags->accept(); else flags->reject();
					});
					control<QPushButton>(graph,"materialShaderFlags")->click(); require(inspected,"full flag lists, masks, tooltips or live words failed");
					require(bytes(nif)==sourceBytes && undo.count()==0,"flags dialog wrote live data");
					control<QPushButton>(graph,"materialGraphApply")->click(); require(graph->result()==QDialog::Accepted,"full flags Apply failed"); done=true;
				} catch(const std::exception &e) { failure=QString::fromUtf8(e.what()); graph->reject(); }
			});
			showMaterialGraph(&nif,index,nullptr); require(done,qPrintable(failure));
			if (!applyAll) { require(bytes(nif)==sourceBytes && undo.count()==0,"inner flags Cancel leaked edits"); continue; }
			auto output=bytes(nif); NifModel reopened; QBuffer in(&output); in.open(QIODevice::ReadOnly); require(reopened.load(in),"full flag words failed reopen");
			for (int word=0;word<2;++word) require(reopened.get<quint32>(reopened.getBlockIndex(shader),QString("Shader Flags %1").arg(word+1))==~original[word],"full flag word changed bits on reopen");
			require(undo.count()==1,"full flag edits not one Undo action"); undo.undo(); require(bytes(nif)==sourceBytes,"full flags Undo not exact"); undo.redo(); require(bytes(nif)==output,"full flags Redo not exact");
			for (int i=0;i<source.getBlockCount();++i) if (i!=shader) {
				QByteArray a,b; QBuffer ba(&a),bb(&b); ba.open(QIODevice::WriteOnly); bb.open(QIODevice::WriteOnly);
				require(source.saveIndex(ba,source.getBlockIndex(i)) && reopened.saveIndex(bb,reopened.getBlockIndex(i)) && a==b,"full flags modified unrelated blocks");
			}
		}
		std::cout << "BS " << source.getBSVersion() << ": all 64 flags, tooltips, live hex, inner Cancel, both high bits, save/reopen, exact Undo/Redo and unrelated blocks passed\n";
		for (int storedCount:{9,6,0}) for (int mode=0;mode<3;++mode) {
			NifModel nif; QBuffer originalInput(&sourceBytes); originalInput.open(QIODevice::ReadOnly); require(nif.load(originalInput),"slot fixture load failed");
			nif.getGameResources().dataPaths.prepend(QString::fromLocal8Bit(argv[2]));
			auto shaderIndex=nif.getBlockIndex(shader); int textures=nif.getLink(shaderIndex,"Texture Set"); auto textureIndex=nif.getBlockIndex(textures);
			require(nif.set<quint32>(textureIndex,"Num Textures",quint32(storedCount)) && nif.updateArraySize(textureIndex,"Textures"),"could not construct shorter texture set");
			auto baseline=bytes(nif); auto originalPaths=nif.getArray<QString>(textureIndex,"Textures"); QUndoStack undo; nif.undoStack=&undo;
			bool done=false; QString failure;
			QTimer::singleShot(0,[&]() {
				auto graph=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!graph) return;
				try {
					for (int slot=0;slot<7;++slot) {
						auto name=QString("materialTexture%1").arg(slot).toLatin1(); auto edit=control<QLineEdit>(graph,name.constData());
						require(edit && edit->isEnabled() && !edit->toolTip().isEmpty(),"one of seven editable texture inputs is missing");
						edit->setText(QString("textures\\materialgraph_slot%1.dds").arg(slot));
					}
					auto input5=control<QLineEdit>(graph,"materialTexture5"),input6=control<QLineEdit>(graph,"materialTexture6");
					require(control<QToolButton>(graph,"materialTextureChoose5") && control<QToolButton>(graph,"materialTextureChoose6"),"new texture browse controls missing");
					control<QToolButton>(graph,"materialTextureClear5")->click(); require(input5->text().isEmpty() && !input6->text().isEmpty(),"mask clear changed another slot"); input5->setText("textures\\materialgraph_slot5.dds");
					if (storedCount==6 && mode==0) {
						bool browser=false; QTimer::singleShot(0,[&]() { auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (dialog && dialog!=graph) { browser=true; dialog->reject(); } });
						control<QToolButton>(graph,"materialTextureChoose6")->click(); require(browser && input6->text()=="textures\\materialgraph_slot6.dds","inner-layer resource picker Cancel changed path");
					}
					require(bytes(nif)==baseline && undo.count()==0,"editing seven slots mutated the live NIF");
					if (mode==0) graph->reject(); else {
						if (mode==2) input6->setText(QString(4097,QChar('x')));
						else {
							auto choice=control<QComboBox>(graph,"materialShaderType"); choice->setCurrentIndex(choice->findData(unsigned(ShaderFlags::ST_EnvironmentMap)));
							control<QLineEdit>(graph,"materialEnvironmentMapScale")->setText("2.5");
						}
						control<QPushButton>(graph,"materialGraphApply")->click();
						if (mode==2) { require(graph->result()!=QDialog::Accepted && bytes(nif)==baseline && undo.count()==0,"invalid new slot was not rejected atomically"); graph->reject(); }
						else require(graph->result()==QDialog::Accepted,qPrintable(control<QLabel>(graph,"materialGraphStatus")->text()));
					}
					done=true;
				} catch(const std::exception &e) { failure=QString::fromUtf8(e.what()); graph->reject(); }
			});
			showMaterialGraph(&nif,shaderIndex,nullptr); require(done,qPrintable(failure));
			if (mode!=1) { require(bytes(nif)==baseline,"seven-slot Cancel changed source"); continue; }
			auto edited=bytes(nif); NifModel reopened; QBuffer in(&edited); in.open(QIODevice::ReadOnly); require(reopened.load(in),"seven slots failed reopen");
			auto paths=reopened.getArray<QString>(reopened.getBlockIndex(textures),"Textures"); require(paths.size()==std::max(storedCount,7),"wrong expanded texture count");
			for (int slot=0;slot<7;++slot) require(paths[slot]==QString("textures\\materialgraph_slot%1.dds").arg(slot),"wrong texture input wrote a slot");
			for (int slot=7;slot<storedCount;++slot) require(paths[slot]==originalPaths[slot],"unexposed trailing texture slot changed");
			require(reopened.get<float>(reopened.getBlockIndex(shader),"Environment Map Scale")==2.5f,"combined shader and texture expansion lost a material value");
			require(undo.count()==1,"seven texture inputs did not commit as one Undo action"); undo.undo(); require(bytes(nif)==baseline,"texture expansion Undo not exact"); undo.redo(); require(bytes(nif)==edited,"texture expansion Redo not exact");
			NifModel original; QBuffer before(&baseline); before.open(QIODevice::ReadOnly); require(original.load(before),"slot baseline reopen failed");
			for (int block=0;block<nif.getBlockCount();++block) if (block!=shader && block!=textures) {
				QByteArray a,b; QBuffer ba(&a),bb(&b); ba.open(QIODevice::WriteOnly); bb.open(QIODevice::WriteOnly);
				require(original.saveIndex(ba,original.getBlockIndex(block)) && nif.saveIndex(bb,nif.getBlockIndex(block)) && a==b,"texture expansion altered unrelated blocks");
			}
			std::cout << "BS " << source.getBSVersion() << ": seven inputs from " << storedCount << " stored entries, combined shader edits, reopen, exact expansion Undo/Redo and trailing/unrelated data passed\n";
		}
		NifModel disk; require(disk.loadFromFile(QString::fromLocal8Bit(argv[1])) && bytes(disk)==sourceBytes,"source file changed"); return 0;
	} catch(const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
