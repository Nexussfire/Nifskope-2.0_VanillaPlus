/***** BEGIN LICENSE BLOCK *****

BSD License

Copyright (c) 2005-2017, NIF File Format Library and Tools
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the NIF File Format Library and Tools project may not be
   used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

***** END LICENCE BLOCK *****/

#include "undocommands.h"

#include "data/nifvalue.h"
#include "model/nifmodel.h"

#include <QCoreApplication>
#include <algorithm>
#include <vector>


//! @file undocommands.cpp ChangeValueCommand, ToggleCheckBoxListCommand

size_t ChangeValueCommand::lastID = 0;

namespace {
class UndoBlockRegistry final : public QObject {
public:
	explicit UndoBlockRegistry(NifModel *model):QObject(model) { setObjectName("undoRecreatedBlockRegistry"); }
	std::vector<std::weak_ptr<UndoBlockHandle>> blocks;
};
UndoBlockRegistry *undoBlocks(NifModel *model,bool create) {
	for(auto child:model->children()) if(auto registry=dynamic_cast<UndoBlockRegistry *>(child)) return registry;
	return create?new UndoBlockRegistry(model):nullptr;
}
UndoIndexReference captureUndoIndex(NifModel *model,QModelIndex index) {
	UndoIndexReference result; auto registry=undoBlocks(model,false); if(!registry) return result;
	auto root=model->getBlockIndex(index);
	for(const auto &weak:registry->blocks) if(auto block=weak.lock();block && block->root==root && root.isValid()) { result.block=block; break; }
	if(!result.block) return result;
	result.column=index.column();
	for(;index.isValid() && index!=root;index=index.parent()) {
		result.rows.prepend(index.row()); result.names.prepend(model->itemName(index)); result.types.prepend(model->itemStrType(index));
	}
	return result;
}
}
std::shared_ptr<UndoBlockHandle> bindUndoBlock(NifModel *model,const QModelIndex &root,std::shared_ptr<UndoBlockHandle> handle) {
	if(!handle) { handle=std::make_shared<UndoBlockHandle>(); auto registry=undoBlocks(model,true);
		std::erase_if(registry->blocks,[](const auto &weak) { return weak.expired(); }); registry->blocks.push_back(handle);
	}
	handle->root=root; return handle;
}
QModelIndex UndoIndexReference::resolve() const {
	if(!block || !block->root.isValid()) return {};
	auto nif=NifModel::fromIndex(block->root); if(!nif) return {};
	QModelIndex index=block->root;
	for(int i=0;i<rows.size();++i) {
		index=nif->getIndex(index,rows[i]);
		if(!index.isValid() || nif->itemName(index)!=names[i] || nif->itemStrType(index)!=types[i]) return {};
	}
	return index.siblingAtColumn(column);
}

/*
 *  ChangeValueCommand
 */

ChangeValueCommand::ChangeValueCommand( const QModelIndex & index,
	const QVariant & value, const QString & valueString, const QString & valueType, NifModel * model )
	: QUndoCommand(), nif( model )
{
	idxs << index;
	recreatedIndices << captureUndoIndex(model,index);
	oldValues << index.data( Qt::EditRole );
	newValues << value;

	localID = lastID;

	auto oldTxt = index.data( Qt::DisplayRole ).toString();
	auto newTxt = valueString;

	if ( !newTxt.isEmpty() )
		setText( QCoreApplication::translate( "ChangeValueCommand", "Set %1 to %2" ).arg( valueType ).arg( newTxt ) );
	else
		setText( QCoreApplication::translate( "ChangeValueCommand", "Modify %1" ).arg( valueType ) );
}

ChangeValueCommand::ChangeValueCommand( const QModelIndex & index, const NifValue & oldVal, 
										const NifValue & newVal, const QString & valueType, NifModel * model )
	: QUndoCommand(), nif( model )
{
	idxs << index;
	recreatedIndices << captureUndoIndex(model,index);
	oldValues << oldVal.toVariant();
	newValues << newVal.toVariant();

	localID = lastID;

	auto oldTxt = oldVal.toString();
	auto newTxt = newVal.toString();

	if ( !newTxt.isEmpty() )
		setText( QCoreApplication::translate( "ChangeValueCommand", "Set %1 to %2" ).arg( valueType ).arg( newTxt ) );
	else
		setText( QCoreApplication::translate( "ChangeValueCommand", "Modify %1" ).arg( valueType ) );
}

void ChangeValueCommand::redo()
{
	//qDebug() << "Redoing";
	Q_ASSERT( idxs.size() == newValues.size() && newValues.size() == oldValues.size() );

	if ( idxs.size() > 1 )
		nif->setState( BaseModel::Processing );

	for ( int i=0;i<idxs.size();++i ) {
		if(!idxs[i].isValid()) idxs[i]=recreatedIndices[i].resolve();
		auto idx=idxs[i];
		if ( idx.isValid() )
			nif->setData( idx, newValues.at( i ), Qt::EditRole );
	}

	if ( idxs.size() > 1 ) {
		nif->restoreState();
		nif->dataChanged( idxs.first(), idxs.last() );
	}

	//qDebug() << nif->data( idx ).toString();
}

void ChangeValueCommand::undo()
{
	//qDebug() << "Undoing";

	if ( idxs.size() > 1 )
		nif->setState( BaseModel::Processing );

	for ( int i=0;i<idxs.size();++i ) {
		if(!idxs[i].isValid()) idxs[i]=recreatedIndices[i].resolve();
		auto idx=idxs[i];
		if ( idx.isValid() )
			nif->setData( idx, oldValues.at( i ), Qt::EditRole );
	}

	if ( idxs.size() > 1 ) {
		nif->restoreState();
		nif->dataChanged( idxs.first(), idxs.last() );
	}

	//qDebug() << nif->data( idx ).toString();
}

int ChangeValueCommand::id() const
{
	return localID;
}

bool ChangeValueCommand::mergeWith( const QUndoCommand * other )
{
	const auto cv = static_cast<const ChangeValueCommand*>(other);

	if ( localID != cv->localID )
		return false;

	idxs << cv->idxs;
	recreatedIndices << cv->recreatedIndices;
	newValues << cv->newValues;
	oldValues << cv->oldValues;

	return true;
}

void ChangeValueCommand::createTransaction()
{
	lastID++;
}


/*
 *  ToggleCheckBoxListCommand
 */

ToggleCheckBoxListCommand::ToggleCheckBoxListCommand( const QModelIndex & index,
	const QVariant & value, const QString & valueType, NifModel * model )
	: QUndoCommand(), nif( model ), idx( index )
{
	recreatedIndex=captureUndoIndex(model,index);
	oldValue = index.data( Qt::EditRole );
	newValue = value;

	auto oldTxt = index.data( Qt::DisplayRole ).toString();

	setText( QCoreApplication::translate( "ToggleCheckBoxListCommand", "Modify %1" ).arg( valueType ) );
}

void ToggleCheckBoxListCommand::redo()
{
	if(!idx.isValid()) idx=recreatedIndex.resolve();
	//qDebug() << "Redoing";
	if ( idx.isValid() )
		nif->setData( idx, newValue, Qt::EditRole );

	//qDebug() << nif->data( idx ).toString();
}

void ToggleCheckBoxListCommand::undo()
{
	if(!idx.isValid()) idx=recreatedIndex.resolve();
	//qDebug() << "Undoing";
	if ( idx.isValid() )
		nif->setData( idx, oldValue, Qt::EditRole );

	//qDebug() << nif->data( idx ).toString();
}

ArrayUpdateCommand::ArrayUpdateCommand( const QModelIndex & index, NifModel * model )
	: QUndoCommand(), nif( model ), idx( index )
{
	setText( QCoreApplication::translate( "ArrayUpdateCommand", "Update Array" ) );
}

void ArrayUpdateCommand::redo()
{
	if ( idx.isValid() ) {
		oldSize = nif->rowCount( idx );
		nif->updateArraySize( idx );
		newSize = nif->rowCount( idx );
	}
}

void ArrayUpdateCommand::undo()
{
	if ( idx.isValid() ) {
		// TODO: Actually attempt to set the array size back
		nif->updateArraySize( idx );
	}
}
