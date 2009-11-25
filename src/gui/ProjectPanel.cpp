/*

                          Firewall Builder

                 Copyright (C) 2008 NetCitadel, LLC

  Author:  alek@codeminders.com
           refactoring and bugfixes: vadim@fwbuilder.org

  $Id$

  This program is free software which we release under the GNU General Public
  License. You may redistribute and/or modify this program under the terms
  of that license as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  To get a copy of the GNU General Public License, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include "../../config.h"
#include "global.h"
#include "utils.h"

#include <fwbuilder/Cluster.h>
#include <fwbuilder/Firewall.h>
#include <fwbuilder/RuleSet.h>
#include <fwbuilder/Policy.h>
#include <fwbuilder/NAT.h>
#include <fwbuilder/Routing.h>
#include "fwbuilder/RuleSet.h"
#include "fwbuilder/Rule.h"
#include "fwbuilder/RuleElement.h"

#include "FWBSettings.h"
#include "FWBTree.h"
#include "FWObjectPropertiesFactory.h"
#include "FWWindow.h"
#include "ProjectPanel.h"
#include "RCS.h"
#include "RuleSetView.h"
#include "findDialog.h"
#include "events.h"
#include "ObjectTreeView.h"

#include <QtDebug>
#include <QMdiSubWindow>
#include <QMdiArea>
#include <QTimer>
#include <QStatusBar>
#include <QFileInfo>
#include <QApplication>
#include <QUndoStack>
#include <QUndoGroup>

#include <iostream>


using namespace Ui;
using namespace libfwbuilder;
using namespace std;



void ProjectPanel::initMain(FWWindow *main)
{
    mainW = main;
    closing = false ;
    mdiWindow = NULL;
    treeReloadPending = false;

    // mdiWindow changes state several times right after it is opened,
    // but we call saveState to store splitter position and its geometry
    // when state changes. Flag "ready" is false after ProjectPanel is created
    // and until FWWindow decides that ProjectPanel is ready for operation.
    // Do not load or save state if flag ready is false.
    ready = false;

    int total_width = DEFAULT_H_SPLITTER_POSITION;
    int total_height = DEFAULT_V_SPLITTER_POSITION;

    if (mainW)
    {
        total_width = mainW->width();
        total_height = mainW->height();
    }

    setMainSplitterPosition(DEFAULT_H_SPLITTER_POSITION,
                            total_width - DEFAULT_H_SPLITTER_POSITION);

    enableAvtoSaveState=true ;
    oldState=-1;

    main->undoGroup->addStack(undoStack);

    connect(m_panel->treeDockWidget, SIGNAL(topLevelChanged(bool)),
            this, SLOT(topLevelChangedForTreePanel(bool)));
    connect(m_panel->treeDockWidget, SIGNAL(visibilityChanged(bool)),
            this, SLOT(visibilityChangedForTreePanel(bool)));

//    connect(m_panel->bottomDockWidget, SIGNAL(topLevelChanged(bool)),
//            this, SLOT(topLevelChangedForBottomPanel(bool)));

    fd  = new findDialog(this, this);
    fd->hide();
}

ProjectPanel::ProjectPanel(QWidget *parent): 
    QWidget(parent), // , Qt::WindowSystemMenuHint|Qt::Window),
    mainW(0),
    rcs(0),
    objectTreeFormat(new FWBTree),
    systemFile(true),
    safeMode(false),
    editingStandardLib(false),
    editingTemplateLib(false),
    ruleSetRedrawPending(false),
    objdb(0),
    fd(0),
    autosaveTimer(new QTimer(static_cast<QObject*>(this))), ruleSetTabIndex(0),
    visibleFirewall(0),
    visibleRuleSet(0),
    lastFirewallIdx(-2),
    changingTabs(false),
    noFirewalls(tr("No firewalls defined")),
    m_panel(0),
    undoStack(0)
{
    if (fwbdebug) qDebug("ProjectPanel constructor");
    m_panel = new Ui::ProjectPanel_q();
    m_panel->setupUi(this);
    m_panel->om->setupProject(this);

    undoStack = new QUndoStack(this);

    setWindowTitle(getPageTitle());

    if (fwbdebug) qDebug("New ProjectPanel  %p", this);
}

ProjectPanel::~ProjectPanel()
{
    if (rcs) delete rcs;
    delete m_panel;
}

QString ProjectPanel::getPageTitle()
{
    QString default_caption = tr("Untitled");
    if (rcs)
    {
        QString caption = rcs->getFileName().section("/",-1,-1);
        if (rcs->isInRCS()) caption= caption + ", rev " + rcs->getSelectedRev();
        if (rcs->isRO()) caption = caption + " " + tr("(read-only)");
        if (caption.isEmpty()) return default_caption;
        return caption;
    }
    else return default_caption;
}

RuleElement* ProjectPanel::getRE(Rule* r, int col )
{
    string ret;
    switch (col)
    {
        case 0: ret=RuleElementSrc::TYPENAME; break;//Object
        case 1: ret=RuleElementDst::TYPENAME; break;//Object
        case 2: ret=RuleElementSrv::TYPENAME; break;//Object
        case 3: ret=RuleElementItf::TYPENAME; break;//Object
        case 4: ret=RuleElementInterval::TYPENAME; break;//Time
        default: return NULL;
    }

    return RuleElement::cast( r->getFirstByType(ret) );
}

void ProjectPanel::restoreRuleSetTab()
{
    if (fwbdebug) qDebug("ProjectPanel::()");
    m_panel->ruleSets->setCurrentIndex(ruleSetTabIndex);

}

void ProjectPanel::loadObjects()
{
    m_panel->om->loadObjects();
}

void ProjectPanel::loadObjects(FWObjectDatabase*)
{
    m_panel->om->loadObjects();
}

void ProjectPanel::clearObjects()
{
    m_panel->om->clearObjects();
}

void ProjectPanel::clearFirewallTabs()
{
    if (fwbdebug) qDebug() << "ProjectPanel::clearFirewallTabs";

    m_panel->ruleSets->hide();

    while (m_panel->ruleSets->count()!=0)
    {
        QWidget *p = m_panel->ruleSets->widget(0);
        m_panel->ruleSets->removeWidget(
            m_panel->ruleSets->widget(m_panel->ruleSets->indexOf(p)));
        delete p;
    }
    m_panel->rulesetname->setText("");
    m_panel->ruleSets->show();
    ruleSetViews.clear();
}

void ProjectPanel::closeRuleSetPanel()
{
    if (fwbdebug) qDebug() << "ProjectPanel::closeRuleSetPanel";
    clearFirewallTabs();
    visibleRuleSet = NULL;
}

void ProjectPanel::ensureObjectVisibleInRules(FWReference *obj)
{
    if (fwbdebug) qDebug() << "ProjectPanel::ensureObjectVisibleInRules";
    FWObject *p=obj;
    while (p && RuleSet::cast(p)==NULL ) p=p->getParent();
    if (p==NULL) return;  // something is broken
    openRuleSet(p);
    getCurrentRuleSetView()->setFocus();
    getCurrentRuleSetView()->selectRE( obj );    
}

RuleSetView * ProjectPanel::getCurrentRuleSetView() 
{
    return dynamic_cast<RuleSetView*>(m_panel->ruleSets->currentWidget());
}


void ProjectPanel::reopenFirewall()
{
    if (fwbdebug)  qDebug("ProjectPanel::reopenFirewall()");

    time_t last_modified = db()->getTimeLastModified();
    if (fwbdebug)
        qDebug("ProjectPanel::reopenFirewall(): checkpoint 1: "
               "dirty=%d last_modified=%s",
               db()->isDirty(), ctime(&last_modified));

    if (ruleSetRedrawPending) return;

    int currentPage = m_panel->ruleSets->currentIndex();

    SelectionMemento memento;

    RuleSetView* rv = dynamic_cast<RuleSetView*>(m_panel->ruleSets->currentWidget());
    if (rv) rv->saveCurrentRowColumn(memento);

    last_modified = db()->getTimeLastModified();
    if (fwbdebug)
        qDebug("ProjectPanel::reopenFirewall(): checkpoint 2: "
               "dirty=%d last_modified=%s",
               db()->isDirty(), ctime(&last_modified));

    // since reopenFirewall deletes and recreates all RuleSetView
    // widgets, it causes significant amount of repaint and
    // flicker. Disable updates for the duration of operation to avoid
    // that.
    m_panel->ruleSets->setUpdatesEnabled(false);

    changingTabs = true;

    clearFirewallTabs();

    last_modified = db()->getTimeLastModified();
    if (fwbdebug)
        qDebug("ProjectPanel::reopenFirewall(): checkpoint 3: "
               "dirty=%d last_modified=%s",
               db()->isDirty(), ctime(&last_modified));
    
    if (visibleRuleSet==NULL) return ;

    for (int i =0 ; i < m_panel->ruleSets->count (); i++)
        m_panel->ruleSets->removeWidget(m_panel->ruleSets->widget(i));

    m_panel->rulesetname->setTextFormat(Qt::RichText);
    updateFirewallName();

    last_modified = db()->getTimeLastModified();
    if (fwbdebug)
        qDebug("ProjectPanel::reopenFirewall(): checkpoint 4: "
               "dirty=%d last_modified=%s",
               db()->isDirty(), ctime(&last_modified));

    RuleSetView* rulesetview = 
        RuleSetView::getRuleSetViewByType(this, visibleRuleSet, NULL);
    if (rulesetview)
    {
        m_panel->ruleSets->addWidget(rulesetview);

        last_modified = db()->getTimeLastModified();
        if (fwbdebug)
            qDebug("ProjectPanel::reopenFirewall(): checkpoint 5: "
                   "dirty=%d last_modified=%s",
                   db()->isDirty(), ctime(&last_modified));
    
        m_panel->ruleSets->setCurrentIndex(currentPage);
        rv = dynamic_cast<RuleSetView*>(m_panel->ruleSets->currentWidget());
        rv->restoreCurrentRowColumn(memento);
    
        changingTabs = false;
        mainW->setEnabledAfterRF();

        m_panel->ruleSets->setUpdatesEnabled(true);
        m_panel->ruleSets->show();
    }
}

int  ProjectPanel::findFirewallInList(FWObject *f)
{
    vector<FWObject*>::iterator i;
    int n=0;
    for (i=firewalls.begin(); i!=firewalls.end(); i++,n++)
    {
        if ( (*i)->getId()==f->getId() ) return n;
    }
    return -1;
}

void ProjectPanel::updateFirewallName()
{
    if (visibleRuleSet==NULL) return ;
    QString name;
    mw->buildEditorTitleAndIcon(visibleRuleSet, ObjectEditor::optNone,
                                &name, NULL, false);
    name = "<b>" + name  + "</b>";
    m_panel->rulesetname->setText(name );
}

void ProjectPanel::openRuleSet(FWObject * obj, bool immediately)
{
    //mw->blankEditor();
    visibleRuleSet = RuleSet::cast(obj);
    if (immediately) redrawRuleSets();
    else registerRuleSetRedrawRequest();
}

void ProjectPanel::selectRules()
{
//    `unselect();
    RuleSetView* rv = dynamic_cast<RuleSetView*>(
        m_panel->ruleSets->currentWidget());
    rv->setFocus();
}

void ProjectPanel::unselectRules()
{
    bool havePolicies = (m_panel->ruleSets->count()!=0);

/* commented this out so that when I hit "Edit" in the object's pop-down
 * menu in a rule, ruleset wont lose focus when object editor is opened.
 * If rule set loses focus, the object's background turns from "selected" color
 * to white and user loses context (which object is shown in the object editor)
 */
    if (havePolicies)
    {
        RuleSetView* rv=dynamic_cast<RuleSetView*>(m_panel->ruleSets->currentWidget());

        if (rv && rv->getSelectedObject()!=getSelectedObject())
        {
            rv->clearFocus();
        }
    }
    mainW->disableActions(havePolicies);
}

void ProjectPanel::editCopy()
{
    if (isManipulatorSelected()) copyObj();
    else
        if (m_panel->ruleSets->count()!=0)
            dynamic_cast<RuleSetView*>(m_panel->ruleSets->currentWidget())->copySelectedObject();
}

void ProjectPanel::editCut()
{
    if (isManipulatorSelected()) cutObj();
    else
        if (m_panel->ruleSets->count()!=0)
            dynamic_cast<RuleSetView*>(m_panel->ruleSets->currentWidget())->cutSelectedObject();
}

void ProjectPanel::editDelete()
{
    if (isManipulatorSelected()) deleteObj();
    //else
    //    if (ruleSets->count()!=0)
    //        dynamic_cast<RuleSetView*>(m_panel->ruleSets->currentWidget())->deleteSelectedObject();
}

void ProjectPanel::editPaste()
{
    if (isManipulatorSelected()) pasteObj();
    else
        if (m_panel->ruleSets->count()!=0)
            dynamic_cast<RuleSetView*>(
                m_panel->ruleSets->currentWidget())->pasteObject();
}

QString ProjectPanel::getDestDir(const QString &fname)
{
    QString destdir = "";

    if (st->getWDir().isEmpty())
    {
        if (fname.isEmpty())
        {
/* need some reasonable default working directory.
 * on Unix will use current dir.
 * on Windows will use user's document dir.
 */
#if defined(Q_OS_WIN32) || defined(Q_OS_MACX)
            destdir = userDataDir.c_str();
#else
            destdir = "";
#endif
        } else
        {
            if (QFileInfo(fname).isDir()) destdir=fname;
            else
                destdir = fname.left( fname.lastIndexOf('/',-1) );
        }
    } else
    {
        destdir=st->getWDir();
    }
    return destdir;
}

void ProjectPanel::setFileName(const QString &fname)
{
    systemFile = false;
    rcs->setFileName(fname);
    db()->setFileName(fname.toLatin1().constData());

    setWindowTitle(getPageTitle());
}

//wrapers for some ObjectManipulator functions

FWObject* ProjectPanel::getCurrentLib()
{
    return m_panel->om->getCurrentLib();
}

void ProjectPanel::updateObjectInTree(FWObject *obj, bool subtree)
{
    m_panel->om->updateObjectInTree(obj, subtree);
}

void ProjectPanel::loadDataFromFw(Firewall *fw)
{
    m_panel->om->loadObjects();

    if (fw)
    {
        m_panel->om->updateObjName(fw,"", false);
        m_panel->om->editObject(fw);
    }
}

void ProjectPanel::insertObjectInTree(FWObject *parent, FWObject *obj)
{
    m_panel->om->insertObjectInTree(parent, obj);
}

FWObject* ProjectPanel::createObject(const QString &objType,
                                     const QString &objName,
                                     FWObject *copyFrom)
{
    return m_panel->om->createObject(objType, objName, copyFrom);
}

FWObject* ProjectPanel::createObject(FWObject *parent,
                                     const QString &objType,
                                     const QString &objName,
                                     FWObject *copyFrom)
{
    return m_panel->om->createObject(parent, objType, objName, copyFrom);
}

void ProjectPanel::moveObject(FWObject *target,
                              FWObject *obj)
{
    m_panel->om->moveObject(target, obj);
}

void ProjectPanel::moveObject(const QString &targetLibName,
                              FWObject *obj)
{
    m_panel->om->moveObject(targetLibName, obj);
}

FWObject* ProjectPanel::pasteTo(FWObject *target, FWObject *obj)
{
    return m_panel->om->pasteTo(target, obj);
}

void ProjectPanel::deleteObject(FWObject *obj,bool openobj)
{
    m_panel->om->deleteObject(obj, openobj);
}

ObjectTreeView* ProjectPanel::getCurrentObjectTree()
{
    return m_panel->om->getCurrentObjectTree();
}

void ProjectPanel::findAllFirewalls (std::list<Firewall *> &fws)
{
    m_panel->om->findAllFirewalls(fws);
}

FWObject* ProjectPanel::duplicateObject(FWObject *target,
                                        FWObject *obj,
                                        const QString &name,
                                        bool  askForAutorename)
{
    return m_panel->om->duplicateObject(target, obj, name, askForAutorename);
}

void ProjectPanel::showDeletedObjects(bool f)
{
    m_panel->om->showDeletedObjects(f);
}

void ProjectPanel::select()
{
    m_panel->om->select();
}

void ProjectPanel::unselect()
{
    m_panel->om->unselect();
}

void ProjectPanel::clearManipulatorFocus()
{
    m_panel->om->clearFocus();
}

void ProjectPanel::copyObj()
{
    m_panel->om->copyObj();
}

bool ProjectPanel::isManipulatorSelected()
{
    return m_panel->om->isSelected();
}

void ProjectPanel::cutObj()
{
    m_panel->om->cutObj();
}

void ProjectPanel::pasteObj()
{
    m_panel->om->pasteObj();
}


void ProjectPanel::newObject()
{
    m_panel->om->newObject();
}

void ProjectPanel::deleteObj()
{
    m_panel->om->delObj();
}

FWObject* ProjectPanel::getSelectedObject()
{
    return m_panel->om->getSelectedObject();
}

void ProjectPanel::reopenCurrentItemParent()
{
    m_panel->om->reopenCurrentItemParent();
}

void ProjectPanel::back()
{
    m_panel->om->back();
}

void ProjectPanel::lockObject()
{
    m_panel->om->lockObject();
}

void ProjectPanel::unlockObject()
{
    m_panel->om->unlockObject();
}

void ProjectPanel::setFDObject(FWObject *o)
{
    fd->setObject(o);
    fd->show();
}
void ProjectPanel::resetFD()
{
    fd->reset();
}

void ProjectPanel::insertRule()
{
    if (visibleRuleSet==NULL || m_panel->ruleSets->count()==0) return;
    getCurrentRuleSetView()->insertRule();
}

void ProjectPanel::addRuleAfterCurrent()
{
    if (visibleRuleSet==NULL || m_panel->ruleSets->count()==0) return;
    getCurrentRuleSetView()->addRuleAfterCurrent();
}

void ProjectPanel::removeRule()
{
    if (visibleRuleSet==NULL || m_panel->ruleSets->count()==0) return;
    getCurrentRuleSetView()->removeRule();
}

void ProjectPanel::moveRule()
{
    if (visibleRuleSet==NULL || m_panel->ruleSets->count()==0) return;
    getCurrentRuleSetView()->moveRule();
}

void ProjectPanel::moveRuleUp()
{
    if (visibleRuleSet==NULL || m_panel->ruleSets->count()==0) return;
    getCurrentRuleSetView()->moveRuleUp();
}

void ProjectPanel::moveRuleDown()
{
    if (visibleRuleSet==NULL || m_panel->ruleSets->count()==0) return;
    getCurrentRuleSetView()->moveRuleDown();
}

void ProjectPanel::copyRule()
{
    if (visibleRuleSet==NULL || m_panel->ruleSets->count()==0) return;
    getCurrentRuleSetView()->copyRule();
}

void ProjectPanel::cutRule()
{
    if (visibleRuleSet==NULL || m_panel->ruleSets->count()==0) return;
    getCurrentRuleSetView()->cutRule();
}

void ProjectPanel::pasteRuleAbove()
{
    if (visibleRuleSet==NULL || m_panel->ruleSets->count()==0) return;
    getCurrentRuleSetView()->pasteRuleAbove();
}

void ProjectPanel::pasteRuleBelow()
{
    if (visibleRuleSet==NULL || m_panel->ruleSets->count()==0) return;
    getCurrentRuleSetView()->pasteRuleBelow();
}

bool ProjectPanel::editingLibrary()
{
    return (rcs!=NULL &&
        ( rcs->getFileName().endsWith(".fwl")) );
}

void ProjectPanel::createRCS(const QString &filename)
{
    rcs = new RCS(filename);
    systemFile = true;
}


QString ProjectPanel::getCurrentFileName()
{
    if (rcs!=NULL)  return rcs->getFileName();
    return "";
}

RCS * ProjectPanel::getRCS()
{
    return rcs;
}

void ProjectPanel::compileThis()
{
    if (visibleRuleSet==NULL) return ;
    set<Firewall*> fw;
    Firewall *f = Firewall::cast(visibleRuleSet->getParent());
    if (f)
    {
        fw.insert(f);
        compile(fw);
    }
}

void ProjectPanel::installThis()
{
    if (visibleRuleSet==NULL) return ;
    set<Firewall*> fw;
    Firewall *f = Firewall::cast(visibleRuleSet->getParent());
    if (f)
    {
        fw.insert(f);
        install(fw);
    }
}

void ProjectPanel::compile()
{
    if (mw->isEditorVisible() &&
        !mw->requestEditorOwnership(NULL,NULL,ObjectEditor::optNone,true))
        return;

    fileSave();
    mainW->compile();
}

void ProjectPanel::compile(set<Firewall*> vf)
{
    if (mw->isEditorVisible() &&
        !mw->requestEditorOwnership(NULL, NULL, ObjectEditor::optNone, true))
        return;

    fileSave();
    mainW->compile(vf);
}

void ProjectPanel::install(set<Firewall*> vf)
{
    mainW->install(vf);
}

void ProjectPanel::install()
{
    mainW->install();
}

void ProjectPanel::transferfw(set<Firewall*> vf)
{
    mainW->transferfw(vf);
}

void ProjectPanel::transferfw()
{
    mainW->transferfw();
}

QString ProjectPanel::printHeader()
{
    QString headerText = rcs->getFileName().section("/",-1,-1);
    if (rcs->isInRCS())
        headerText = headerText + ", rev " + rcs->getSelectedRev();
    return headerText;
}

bool ProjectPanel::validateForInsertion(FWObject *target, FWObject *obj)
{
    return objectTreeFormat->validateForInsertion(target, obj);
}

/*
 * TODO: move get*MenuState methods to ObjectManipulator
 */
bool ProjectPanel::getCopyMenuState(const QString &objPath)
{
    return objectTreeFormat->getCopyMenuState(objPath);
}

bool ProjectPanel::getCutMenuState(const QString &objPath)
{
    return objectTreeFormat->getCutMenuState(objPath);
}

bool ProjectPanel::getPasteMenuState(const QString &objPath)
{
    return objectTreeFormat->getPasteMenuState(objPath);
}

bool ProjectPanel::getDeleteMenuState(FWObject *obj)
{
    QString objPath = obj->getPath(true).c_str();
    bool del_menu_item_state = objectTreeFormat->getDeleteMenuState(objPath);

    // can't delete last policy, nat and routing child objects
    // also can't delete "top" policy ruleset
    if (del_menu_item_state && RuleSet::cast(obj))
    {
        //if (dynamic_cast<RuleSet*>(obj)->isTop()) del_menu_item_state = false;
        //else
        //{
            FWObject *fw = obj->getParent();
            // fw can be NULL if this ruleset is in the Deleted objects
            // library
            if (fw==NULL) return del_menu_item_state;
            list<FWObject*> child_objects = fw->getByType(obj->getTypeName());
            if (child_objects.size()==1) del_menu_item_state = false;
        //}
    }
    return del_menu_item_state;
}

FWObject* ProjectPanel::createNewLibrary(FWObjectDatabase *db)
{
    return objectTreeFormat->createNewLibrary(db);
}

void ProjectPanel::registerRuleSetRedrawRequest()
{
    if (!ruleSetRedrawPending)
    {
        ruleSetRedrawPending = true;
        //redrawRuleSets();
        QTimer::singleShot( 0, this, SLOT(redrawRuleSets()) );
    }
}

void ProjectPanel::redrawRuleSets()
{
    ruleSetRedrawPending = false;
    reopenFirewall();
}

void ProjectPanel::showEvent(QShowEvent *ev)
{ 
    if (fwbdebug) qDebug("ProjectPanel::showEvent %p title=%s",
                         this, getPageTitle().toAscii().constData());
    QWidget::showEvent(ev);
}

void ProjectPanel::hideEvent(QHideEvent *ev)
{
    if (fwbdebug) qDebug("ProjectPanel::hideEvent %p title=%s",
                         this, getPageTitle().toAscii().constData());
    QWidget::hideEvent(ev);
}

void ProjectPanel::closeEvent(QCloseEvent * ev)
{   
    if (fwbdebug)
        qDebug() << "ProjectPanel::closeEvent title=" << getPageTitle();

    if (!saveIfModified() || !checkin(true))
    {
        ev->ignore();
        return;
    }

    saveState();
    fileClose();

    mw->updateWindowTitle();

    QTimer::singleShot( 0, mw, SLOT(projectWindowClosed()) );
}

QString ProjectPanel::getFileName()
{
    if (rcs!=NULL)
    {
        QString FileName = rcs->getFileName();
        return FileName;
    }
    else
        return "";
}

void ProjectPanel::splitterMoved(int , int)
{
}

void ProjectPanel::resizeEvent(QResizeEvent*)
{
}

void ProjectPanel::registerTreeReloadRequest()
{
    treeReloadPending = true;
    QTimer::singleShot(0, this, SLOT(reloadTree()));
}

void ProjectPanel::reloadTree()
{
    if (treeReloadPending)
    {
        m_panel->om->reload();
        treeReloadPending = false;
    }
}

void ProjectPanel::registerObjectToUpdateInTree(FWObject *o, bool update_subtree)
{
    if (fwbdebug)
        qDebug() << "ProjectPanel::registerObjectToUpdateInTree()"
                 << "o=" << o->getName().c_str()
                 << "update_subtree=" << update_subtree
                 << "updateObjectsInTreePool.size()=" << updateObjectsInTreePool.size();
    updateObjectsInTreePool[o->getId()] = update_subtree;
    QTimer::singleShot(0, this, SLOT(updateObjectInTree()));
}

void ProjectPanel::updateObjectInTree()
{
    if (fwbdebug)
        qDebug() << "ProjectPanel::updateObjectInTree()"
                 << "updateObjectsInTreePool.size()=" << updateObjectsInTreePool.size();

    while (updateObjectsInTreePool.size() > 0)
    {
        map<int, bool>::iterator it = updateObjectsInTreePool.begin();
        FWObject *obj = db()->findInIndex(it->first);
        m_panel->om->updateObjectInTree(obj, it->second);
        updateObjectsInTreePool.erase(it);
    }
    mdiWindow->update();
}

void ProjectPanel::registerModifiedObject(FWObject *o)
{
    lastModifiedTimestampChangePool.insert(o->getId());
    QTimer::singleShot(0, this, SLOT(updateLastModifiedTimestampForAllFirewalls()));
}

void ProjectPanel::updateLastModifiedTimestampForAllFirewalls()
{
    if (fwbdebug)
        qDebug() << "ProjectPanel::updateLastModifiedTimestampForAllFirewalls"
                 << "lastModifiedTimestampChangePool.size()="
                 << lastModifiedTimestampChangePool.size();

    if (lastModifiedTimestampChangePool.size() == 0) return;

    QStatusBar *sb = mw->statusBar();
    sb->showMessage( tr("Searching for firewalls affected by the change...") );

    //QApplication::processEvents(QEventLoop::ExcludeUserInputEvents,100);

    QApplication::setOverrideCursor(QCursor( Qt::WaitCursor));

    set<Firewall*> firewalls_to_update;

    while (lastModifiedTimestampChangePool.size() > 0)
    {
        set<int>::iterator it = lastModifiedTimestampChangePool.begin();
        FWObject *obj = db()->findInIndex(*it);
        lastModifiedTimestampChangePool.erase(it);

        if (fwbdebug)
            qDebug() << "Modified object: " << obj->getName().c_str();

        if (FWBTree().isSystem(obj)) continue;

        list<Firewall *> fws = m_panel->om->findFirewallsForObject(obj);
        if (fws.size())
        {
            Firewall *f;
            for (list<Firewall *>::iterator i=fws.begin();
                 i!=fws.end();
                 ++i)
            {
                f = *i;
                if (f==obj) continue;

                firewalls_to_update.insert(f);
            }
        }
    }

    if (fwbdebug)
        qDebug() << "Will update " << firewalls_to_update.size() << " firewalls";

    for (set<Firewall*>::iterator it=firewalls_to_update.begin();
         it!=firewalls_to_update.end(); ++it)
    {
        Firewall * f = *it;

        f->updateLastModifiedTimestamp();
        QCoreApplication::postEvent(
            mw, new updateObjectInTreeEvent(getFileName(), f->getId()));

        list<Cluster*> clusters = m_panel->om->findClustersUsingFirewall(f);
        if (clusters.size() != 0)
        {
            list<Cluster*>::iterator it;
            for (it=clusters.begin(); it!=clusters.end(); ++it)
            {
                Cluster *cl = *it;
                cl->updateLastModifiedTimestamp();
                QCoreApplication::postEvent(
                    mw, new updateObjectInTreeEvent(getFileName(), cl->getId()));
            }
        }
    }

    QApplication::restoreOverrideCursor();
    sb->clearMessage();
//    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents,100);
}

void ProjectPanel::toggleViewTree(bool f)
{
    if (f) m_panel->treeDockWidget->show();
    else m_panel->treeDockWidget->hide();
}

/*
 * Signal QDockWidget::topLevelChanged is called after dock widget
 * is made floating or docked.
 */
void ProjectPanel::topLevelChangedForTreePanel(bool f)
{
    if (fwbdebug)
        qDebug() << "ProjectPanel::topLevelChangedForTreePanel  f=" << f;

    /*
     * QDockWidget object uses native decorators on Windows and Mac
     * and therefore gets window title bar there. On X11 QT emulates
     * title bar and allows dragging of the floating dock widget only
     * if its parent is QMainWindow. Here is a hack: we reparent the
     * widget in order to satisfy their requirements and make floating
     * panel widget draggable on all platforms. Need to reparent it
     * back and stick it into the layout of the ProjectPanel when it
     * is docked.
     */
    m_panel->treeDockWidget->disconnect(SIGNAL(topLevelChanged(bool)));
    m_panel->treeDockWidget->disconnect(SIGNAL(visibilityChanged(bool)));
    
    if (f)
    {
        m_panel->treeDockWidget->setParent(mw);
        mw->addDockWidget(Qt::LeftDockWidgetArea, m_panel->treeDockWidget);
        m_panel->treeDockWidget->show();
    } else
    {
        mw->removeDockWidget(m_panel->treeDockWidget);
        m_panel->treeDockWidget->setParent(m_panel->treeDockWidgetParentFrame);
        m_panel->treeDockWidgetParentFrame->layout()->addWidget(m_panel->treeDockWidget);
        m_panel->treeDockWidget->show();
    }
    m_panel->treeDockWidget->setFloating(f);

    connect(m_panel->treeDockWidget, SIGNAL(topLevelChanged(bool)),
            this, SLOT(topLevelChangedForTreePanel(bool)));
    connect(m_panel->treeDockWidget, SIGNAL(visibilityChanged(bool)),
            this, SLOT(visibilityChangedForTreePanel(bool)));

    if (!m_panel->treeDockWidget->isWindow())
    {
        loadMainSplitter();
    } else
    {
        saveMainSplitter();
        // expand rules 
        collapseTree();
        m_panel->treeDockWidget->widget()->update();
    }
}

void ProjectPanel::visibilityChangedForTreePanel(bool f)
{
    if (fwbdebug)
        qDebug() << "ProjectPanel::visibilityChangedForTreePanel  f="
                 << f
                 << " m_panel->treeDockWidget->isWindow()="
                 << m_panel->treeDockWidget->isWindow();

    if (f && !m_panel->treeDockWidget->isWindow())  // visible and not floating
    {
        loadMainSplitter();
    } else
    {
        saveMainSplitter();
        // expand rules 
        collapseTree();
        m_panel->treeDockWidget->widget()->update();
    }
}

void ProjectPanel::setActive()
{
    undoStack->setActive(true);
}
