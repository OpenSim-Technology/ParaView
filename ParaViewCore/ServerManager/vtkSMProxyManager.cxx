/*=========================================================================

  Program:   ParaView
  Module:    vtkSMProxyManager.cxx

  Copyright (c) Kitware, Inc.
  All rights reserved.
  See Copyright.txt or http://www.paraview.org/HTML/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkSMProxyManager.h"

#include "vtkCollection.h"
#include "vtkEventForwarderCommand.h"
#include "vtkInstantiator.h"
#include "vtkObjectFactory.h"
#include "vtkProcessModule.h"
#include "vtkProcessModule.h"
#include "vtkPVConfig.h" // for PARAVIEW_VERSION_*
#include "vtkPVProxyDefinitionIterator.h"
#include "vtkPVXMLElement.h"
#include "vtkPVXMLParser.h"
#include "vtkReservedRemoteObjectIds.h"
#include "vtkSmartPointer.h"
#include "vtkSMDocumentation.h"
#include "vtkSMGlobalPropertiesLinkUndoElement.h"
#include "vtkSMPipelineState.h"
#include "vtkSMPropertyIterator.h"
#include "vtkSMProxyDefinitionManager.h"
#include "vtkSMProxy.h"
#include "vtkSMProxyIterator.h"
#include "vtkSMProxyLocator.h"
#include "vtkSMProxyProperty.h"
#include "vtkSMReaderFactory.h"
#include "vtkSMStateLoader.h"
#include "vtkSMStateLocator.h"
#include "vtkSMUndoStackBuilder.h"
#include "vtkSMUndoStack.h"
#include "vtkSMWriterFactory.h"
#include "vtkStdString.h"
#include "vtkStringList.h"
#include "vtkProcessModule.h"
#include "vtkSMGlobalPropertiesLinkUndoElement.h"
#include "vtkSMUndoStackBuilder.h"
#include "vtkSMDeserializerProtobuf.h"
#include "vtkSMProxyLocator.h"

#include "vtkSMSessionClient.h"
#include "vtkSMCollaborationManager.h"

#include <vtkstd/map>
#include <vtkstd/set>
#include <vtkstd/vector>
#include <vtksys/DateStamp.h> // For date stamp
#include <vtksys/ios/sstream>
#include <vtksys/RegularExpression.hxx>
#include <assert.h>
#include <vtkNew.h>

#include "vtkSMProxyManagerInternals.h"

#if 0 // for debugging
class vtkSMProxyRegObserver : public vtkCommand
{
public:
  virtual void Execute(vtkObject*, unsigned long event, void* data)
    {
      vtkSMProxyManager::RegisteredProxyInformation* info =
        (vtkSMProxyManager::RegisteredProxyInformation*)data;
      cout << info->Proxy
           << " " << vtkCommand::GetStringFromEventId(event)
           << " " << info->GroupName
           << " " << info->ProxyName
           << endl;
    }
};
#endif


#define PARAVIEW_SOURCE_VERSION "paraview version " PARAVIEW_VERSION_FULL ", Date: " vtksys_DATE_STAMP_STRING

class vtkSMProxyManagerProxySet : public vtkstd::set<vtkSMProxy*> {};

//*****************************************************************************
class vtkSMProxyManagerObserver : public vtkCommand
{
public:
  static vtkSMProxyManagerObserver* New()
    { return new vtkSMProxyManagerObserver(); }

  void SetTarget(vtkSMProxyManager* t)
    {
    this->Target = t;
    }

  virtual void Execute(vtkObject *obj, unsigned long event, void* data)
    {
    if (this->Target)
      {
      this->Target->ExecuteEvent(obj, event, data);
      }
    }

protected:
  vtkSMProxyManagerObserver()
    {
    this->Target = 0;
    }
  vtkSMProxyManager* Target;
};
//*****************************************************************************
vtkStandardNewMacro(vtkSMProxyManager);
//---------------------------------------------------------------------------
vtkSMProxyManager::vtkSMProxyManager()
{
  this->StateUpdateNotification = true;
  this->Session = NULL;
  this->PipelineState = NULL;
  this->UpdateInputProxies = 0;
  this->Internals = new vtkSMProxyManagerInternals;
  this->Observer = vtkSMProxyManagerObserver::New();
  this->Observer->SetTarget(this);
#if 0 // for debugging
  vtkSMProxyRegObserver* obs = new vtkSMProxyRegObserver;
  this->AddObserver(vtkCommand::RegisterEvent, obs);
  this->AddObserver(vtkCommand::UnRegisterEvent, obs);
#endif

  this->ProxyDefinitionManager = vtkSMProxyDefinitionManager::New();
  this->ProxyDefinitionManager->AddObserver(
    vtkCommand::RegisterEvent, this->Observer);
  this->ProxyDefinitionManager->AddObserver(
    vtkCommand::UnRegisterEvent, this->Observer);
  this->ProxyDefinitionManager->AddObserver(
    vtkSMProxyDefinitionManager::ProxyDefinitionsUpdated, this->Observer);
  this->ProxyDefinitionManager->AddObserver(
    vtkSMProxyDefinitionManager::CompoundProxyDefinitionsUpdated, this->Observer);

  this->ReaderFactory = vtkSMReaderFactory::New();
  this->WriterFactory = vtkSMWriterFactory::New();
  this->WriterFactory->SetProxyManager(this);

  // Provide internal object a pointer to us
  this->Internals->ProxyManager = this;

}

//---------------------------------------------------------------------------
vtkSMProxyManager::~vtkSMProxyManager()
{
  // This is causing a PushState() when the object is being destroyed. This
  // causes errors since the ProxyManager is destroyed only when the session is
  // being deleted, thus the session cannot be valid at this point.
  //this->UnRegisterProxies();
  delete this->Internals;

  this->Observer->SetTarget(0);
  this->Observer->Delete();

  this->ReaderFactory->Delete();
  this->ReaderFactory = 0;

  this->WriterFactory->SetProxyManager(0);
  this->WriterFactory->Delete();
  this->WriterFactory = 0;

  this->ProxyDefinitionManager->Delete();
  this->ProxyDefinitionManager = NULL;

  if(this->PipelineState)
    {
    this->PipelineState->Delete();
    this->PipelineState = NULL;
    }
}

//----------------------------------------------------------------------------
vtkTypeUInt32 vtkSMProxyManager::GetReservedGlobalID()
{
  return vtkReservedRemoteObjectIds::RESERVED_PROXY_MANAGER_ID;
}

//----------------------------------------------------------------------------
const char* vtkSMProxyManager::GetParaViewSourceVersion()
{
  return PARAVIEW_SOURCE_VERSION;
}

//----------------------------------------------------------------------------
int vtkSMProxyManager::GetVersionMajor()
{
  return PARAVIEW_VERSION_MAJOR;
}

//----------------------------------------------------------------------------
int vtkSMProxyManager::GetVersionMinor()
{
  return PARAVIEW_VERSION_MINOR;
}

//----------------------------------------------------------------------------
int vtkSMProxyManager::GetVersionPatch()
{
  return PARAVIEW_VERSION_PATCH;
}

//----------------------------------------------------------------------------
void vtkSMProxyManager::SetSession(vtkSMSession* session)
{
  if (this->Session == session)
    {
    return;
    }
  if (this->Session)
    {
    this->UnRegisterAllLinks();
    this->UnRegisterProxies();
    this->PipelineState->Delete();
    this->PipelineState = NULL;

    // Remove previously created selection models
    this->Internals->SelectionModels.clear();
    this->Internals->UpdateProxySelectionModelState();
    }

  this->Session = session;

  if (this->Session)
    {
    // This will also register the RemoteObject to the Session
    this->PipelineState = vtkSMPipelineState::New();
    this->PipelineState->SetSession(this->Session);
    this->ProxyDefinitionManager->SetSession(session);
    }
}
//----------------------------------------------------------------------------
void vtkSMProxyManager::UpdateFromRemote()
{
  if (this->Session)
    {
    // For collaboration purpose at connection time we synchronize our
    // ProxyManager state with the server
    if(this->Session->IsA("vtkSMSessionClient"))
      {
      vtkSMMessage msg;
      msg.set_global_id(vtkSMProxyManager::GetReservedGlobalID());
      msg.set_location(vtkPVSession::DATA_SERVER_ROOT);
      this->Session->PullState(&msg);
      if(msg.ExtensionSize(PXMRegisteredProxyState::registered_proxy) > 0)
        {
        // We take the parent locator to always refer to the server while loading
        // the state. This prevent us from getting a creation state instead of
        // full update state when we split the creation/update action in 2
        // separate call.
        // Moreover, we don't want any existing states to be pushed again to
        // the server.
        this->Session->StartProcessingRemoteNotification();

        // Setup server only state/proxy Locator
        vtkNew<vtkSMDeserializerProtobuf> deserializer;
        deserializer->SetStateLocator(this->Session->GetStateLocator()->GetParentLocator());
        deserializer->SetSession(this->Session);

        vtkNew<vtkSMProxyLocator> serverLocator;
        serverLocator->SetDeserializer(deserializer.GetPointer());
        serverLocator->UseSessionToLocateProxy(true);
        serverLocator->SetSession(this->Session);

        // Load and update
        vtkSMProxyProperty::EnableProxyCreation();
        this->LoadState( &msg, serverLocator.GetPointer());
        this->UpdateRegisteredProxies(0);
        vtkSMProxyProperty::DisableProxyCreation();

        this->Session->StopProcessingRemoteNotification();
        }
      }
    }
}

//----------------------------------------------------------------------------
void vtkSMProxyManager::InstantiateGroupPrototypes(const char* groupName)
{
  if (!groupName)
    {
    return;
    }

  assert(this->ProxyDefinitionManager != 0);

  vtksys_ios::ostringstream newgroupname;
  newgroupname << groupName << "_prototypes" << ends;

  // Not a huge fan of this iterator API. Need to make it more consistent with
  // VTK.
  vtkPVProxyDefinitionIterator* iter =
      this->ProxyDefinitionManager->NewSingleGroupIterator(groupName);

  // Find the XML elements from which the proxies can be instantiated and
  // initialized
  for ( iter->InitTraversal(); !iter->IsDoneWithTraversal();
        iter->GoToNextItem())
    {
    const char* xml_name = iter->GetProxyName();
    if (this->GetProxy(newgroupname.str().c_str(), xml_name) == NULL)
      {
      vtkSMProxy* proxy = this->NewProxy(groupName, xml_name);
      if (proxy)
        {
        proxy->SetSession(NULL);
        proxy->SetLocation(0);
        proxy->SetPrototype(true);
        this->RegisterProxy(newgroupname.str().c_str(), xml_name, proxy);
        proxy->FastDelete();
        }
      }
    }
  iter->Delete();
}

//----------------------------------------------------------------------------
void vtkSMProxyManager::InstantiatePrototypes()
{
  assert(this->ProxyDefinitionManager != 0);
  vtkPVProxyDefinitionIterator* iter =
    this->ProxyDefinitionManager->NewIterator();

  for (iter->InitTraversal(); !iter->IsDoneWithTraversal();
    iter->GoToNextGroup())
    {
    this->InstantiateGroupPrototypes(iter->GetGroupName());
    }
}

//----------------------------------------------------------------------------
vtkSMProxy* vtkSMProxyManager::NewProxy(
  const char* groupName, const char* proxyName, const char* subProxyName)
{
  if (!groupName || !proxyName)
    {
    return 0;
    }
  // Find the XML element from which the proxy can be instantiated and
  // initialized
  vtkPVXMLElement* element = this->GetProxyElement( groupName, proxyName,
                                                    subProxyName);
  if (element)
    {
    return this->NewProxy(element, groupName, proxyName, subProxyName);
    }

  return 0;
}

//---------------------------------------------------------------------------
vtkSMProxy* vtkSMProxyManager::NewProxy(vtkPVXMLElement* pelement,
                                        const char* groupname,
                                        const char* proxyname,
                                        const char* subProxyName)
{
  vtkObject* object = 0;
  vtksys_ios::ostringstream cname;
  cname << "vtkSM" << pelement->GetName() << ends;
  object = vtkInstantiator::CreateInstance(cname.str().c_str());

  vtkSMProxy* proxy = vtkSMProxy::SafeDownCast(object);
  if (proxy)
    {
    // XMLName/XMLGroup should be set before ReadXMLAttributes so sub proxy
    // can be found based on their names when sent to the PM Side
    proxy->SetXMLGroup(groupname);
    proxy->SetXMLName(proxyname);
    proxy->SetXMLSubProxyName(subProxyName);
    proxy->SetSession(this->GetSession());
    proxy->ReadXMLAttributes(this, pelement);
    }
  else
    {
    vtkWarningMacro("Creation of new proxy " << cname.str() << " failed ("
                    << groupname << ", " << proxyname << ").");
    }
  return proxy;
}


//---------------------------------------------------------------------------
vtkSMDocumentation* vtkSMProxyManager::GetProxyDocumentation(
  const char* groupName, const char* proxyName)
{
  if (!groupName || !proxyName)
    {
    return 0;
    }

  vtkSMProxy* proxy = this->GetPrototypeProxy(groupName, proxyName);
  return proxy? proxy->GetDocumentation() : NULL;
}

//---------------------------------------------------------------------------
vtkSMDocumentation* vtkSMProxyManager::GetPropertyDocumentation(
  const char* groupName, const char* proxyName, const char* propertyName)
{
  if (!groupName || !proxyName || !propertyName)
    {
    return 0;
    }

  vtkSMProxy* proxy = this->GetPrototypeProxy(groupName, proxyName);
  if (proxy)
    {
    vtkSMProperty* prop = proxy->GetProperty(propertyName);
    if (prop)
      {
      return prop->GetDocumentation();
      }
    }
  return 0;
}

//---------------------------------------------------------------------------
vtkPVXMLElement* vtkSMProxyManager::GetProxyElement(const char* groupName,
                                                    const char* proxyName,
                                                    const char* subProxyName)
{
  assert(this->ProxyDefinitionManager != 0);
  return this->ProxyDefinitionManager->GetCollapsedProxyDefinition( groupName,
                                                                    proxyName,
                                                                    subProxyName,
                                                                    true);
}

//---------------------------------------------------------------------------
unsigned int vtkSMProxyManager::GetNumberOfProxies(const char* group)
{
  vtkSMProxyManagerInternals::ProxyGroupType::iterator it =
    this->Internals->RegisteredProxyMap.find(group);
  if ( it != this->Internals->RegisteredProxyMap.end() )
    {
    int size = 0;
    vtkSMProxyManagerProxyMapType::iterator it2 =
      it->second.begin();
    for (; it2 != it->second.end(); ++it2)
      {
      size += it2->second.size();
      }
    return size;
    }
  return 0;
}

//---------------------------------------------------------------------------
// No errors are raised if a proxy definition for the requested proxy is not
// found.
vtkSMProxy* vtkSMProxyManager::GetPrototypeProxy(const char* groupname,
  const char* name)
{
  if (!this->ProxyDefinitionManager)
    {
    return NULL;
    }

  vtkstd::string protype_group = groupname;
  protype_group += "_prototypes";
  vtkSMProxy* proxy = this->GetProxy(protype_group.c_str(), name);
  if (proxy)
    {
    return proxy;
    }

  // silently ask for the definition. If not found return NULL.
  vtkPVXMLElement* xmlElement =
    this->ProxyDefinitionManager->GetCollapsedProxyDefinition(
      groupname, name, NULL, false);
  if (xmlElement == NULL)
    {
    // No definition was located for the requested proxy.
    // Cannot create the prototype.
    return 0;
    }

  proxy = this->NewProxy(groupname, name);
  if (!proxy)
    {
    return 0;
    }
  proxy->SetSession(NULL);
  proxy->SetLocation(0);
  proxy->SetPrototype(true);
  // register the proxy as a prototype.
  this->RegisterProxy(protype_group.c_str(), name, proxy);
  proxy->Delete();
  return proxy;
}

//---------------------------------------------------------------------------
vtkSMProxy* vtkSMProxyManager::GetProxy(const char* group, const char* name)
{
  vtkSMProxyManagerInternals::ProxyGroupType::iterator it =
    this->Internals->RegisteredProxyMap.find(group);
  if ( it != this->Internals->RegisteredProxyMap.end() )
    {
    vtkSMProxyManagerProxyMapType::iterator it2 =
      it->second.find(name);
    if (it2 != it->second.end())
      {
      if (it2->second.begin() != it2->second.end())
        {
        return it2->second.front()->Proxy.GetPointer();
        }
      }
    }
  return 0;
}

//---------------------------------------------------------------------------
vtkSMProxy* vtkSMProxyManager::GetProxy(const char* name)
{
  vtkSMProxyManagerInternals::ProxyGroupType::iterator it =
    this->Internals->RegisteredProxyMap.begin();
  for (; it != this->Internals->RegisteredProxyMap.end(); it++)
    {
    vtkSMProxyManagerProxyMapType::iterator it2 =
      it->second.find(name);
    if (it2 != it->second.end())
      {
      if (it2->second.begin() != it2->second.end())
        {
        return it2->second.front()->Proxy.GetPointer();
        }
      }
    }
  return 0;
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::GetProxies(const char* group,
  const char* name, vtkCollection* collection)
{
  collection->RemoveAllItems();
  vtkSMProxyManagerInternals::ProxyGroupType::iterator it =
    this->Internals->RegisteredProxyMap.find(group);
  if(it != this->Internals->RegisteredProxyMap.end())
    {
    if(name == NULL)
      {
      vtkSMProxyManagerProxyMapType::iterator it2 =
        it->second.begin();
      vtkstd::set<vtkTypeUInt32> ids;
      for (; it2 != it->second.end(); it2++)
        {
        vtkSMProxyManagerProxyListType::iterator it3 = it2->second.begin();
        for (; it3 != it2->second.end(); ++it3)
          {
          if(ids.find(it3->GetPointer()->Proxy->GetGlobalID()) != ids.end())
            {
            ids.insert(it3->GetPointer()->Proxy->GetGlobalID());
            collection->AddItem(it3->GetPointer()->Proxy);
            }
          }
        }
      }
    else
      {
      vtkSMProxyManagerProxyMapType::iterator it2 = it->second.find(name);
      if (it2 != it->second.end())
        {
        vtkSMProxyManagerProxyListType::iterator it3 = it2->second.begin();
        for (; it3 != it2->second.end(); ++it3)
          {
          collection->AddItem(it3->GetPointer()->Proxy);
          }
        }
      }
    }
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::GetProxyNames(const char* groupname,
                                      vtkSMProxy* proxy, vtkStringList* names)
{
  if (!names)
    {
    return;
    }
  names->RemoveAllItems();

  if (!groupname || !proxy)
    {
    return;
    }

  vtkSMProxyManagerInternals::ProxyGroupType::iterator it =
    this->Internals->RegisteredProxyMap.find(groupname);
  if ( it != this->Internals->RegisteredProxyMap.end() )
    {
    vtkSMProxyManagerProxyMapType::iterator it2 =
      it->second.begin();
    for (; it2 != it->second.end(); it2++)
      {
      if (it2->second.Contains(proxy))
        {
        names->AddString(it2->first.c_str());
        }
      }
    }
}


//---------------------------------------------------------------------------
const char* vtkSMProxyManager::GetProxyName(const char* groupname,
                                            vtkSMProxy* proxy)
{
  if (!groupname || !proxy)
    {
    return 0;
    }

  vtkSMProxyManagerInternals::ProxyGroupType::iterator it =
    this->Internals->RegisteredProxyMap.find(groupname);
  if ( it != this->Internals->RegisteredProxyMap.end() )
    {
    vtkSMProxyManagerProxyMapType::iterator it2 =
      it->second.begin();
    for (; it2 != it->second.end(); it2++)
      {
      if (it2->second.Contains(proxy))
        {
        return it2->first.c_str();
        }
      }
    }

  return 0;
}

//---------------------------------------------------------------------------
const char* vtkSMProxyManager::GetProxyName(const char* groupname,
                                            unsigned int idx)
{
  if (!groupname)
    {
    return 0;
    }

  unsigned int counter=0;

  vtkSMProxyManagerInternals::ProxyGroupType::iterator it =
    this->Internals->RegisteredProxyMap.find(groupname);
  if ( it != this->Internals->RegisteredProxyMap.end() )
    {
    vtkSMProxyManagerProxyMapType::iterator it2 =
      it->second.begin();
    for (; it2 != it->second.end(); it2++)
      {
      if (counter == idx)
        {
        return it2->first.c_str();
        }
      counter++;
      }
    }

  return 0;
}

//---------------------------------------------------------------------------
const char* vtkSMProxyManager::IsProxyInGroup(vtkSMProxy* proxy,
                                              const char* groupname)
{
  if (!proxy || !groupname)
    {
    return 0;
    }
  vtkSMProxyManagerInternals::ProxyGroupType::iterator it =
    this->Internals->RegisteredProxyMap.find(groupname);
  if ( it != this->Internals->RegisteredProxyMap.end() )
    {
    vtkSMProxyManagerProxyMapType::iterator it2 =
      it->second.begin();
    for (; it2 != it->second.end(); it2++)
      {
      if (it2->second.Contains(proxy))
        {
        return it2->first.c_str();
        }
      }
    }
  return 0;
}

namespace
{
  struct vtkSMProxyManagerProxyInformation
    {
    vtkstd::string GroupName;
    vtkstd::string ProxyName;
    vtkSMProxy* Proxy;
    };
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::UnRegisterProxies()
{

  // Clear internal proxy containers
  vtkstd::vector<vtkSMProxyManagerProxyInformation> toUnRegister;
  vtkSMProxyIterator* iter = vtkSMProxyIterator::New();
  iter->SetModeToAll();
  for (iter->Begin(); !iter->IsAtEnd(); iter->Next())
    {
    vtkSMProxyManagerProxyInformation info;
    info.GroupName = iter->GetGroup();
    info.ProxyName = iter->GetKey();
    info.Proxy = iter->GetProxy();
    toUnRegister.push_back(info);
    }
  iter->Delete();

  vtkstd::vector<vtkSMProxyManagerProxyInformation>::iterator vIter =
    toUnRegister.begin();
  for (;vIter != toUnRegister.end(); ++vIter)
    {
    this->UnRegisterProxy(vIter->GroupName.c_str(), vIter->ProxyName.c_str(),
      vIter->Proxy);
    }

  this->Internals->ModifiedProxies.clear();
  this->Internals->RegisteredProxyTuple.clear();
  this->Internals->State.ClearExtension(PXMRegisteredProxyState::registered_proxy);

  // Push state for undo/redo BUT only if it is not a clean up before deletion.
  if(this->PipelineState->GetSession())
    {
    this->TriggerStateUpdate();
    }
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::UnRegisterProxy( const char* group, const char* name,
                                         vtkSMProxy* proxy)
{
  // Just in case a proxy ref is NOT held outside the ProxyManager iteself
  // Keep one during the full method call so the event could still have a valid
  // proxy object.
  vtkSmartPointer<vtkSMProxy> proxyHolder = proxy;

  // Do something only if the given tuple was found
  if(this->Internals->RemoveTuples(group, name, proxy))
    {
    RegisteredProxyInformation info;
    info.Proxy = proxy;
    info.GroupName = group;
    info.ProxyName = name;
    info.Type = RegisteredProxyInformation::PROXY;

    this->InvokeEvent(vtkCommand::UnRegisterEvent, &info);
    this->UnMarkProxyAsModified(info.Proxy);

    // Push state for undo/redo
    this->TriggerStateUpdate();
    }
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::UnRegisterProxy(const char* name)
{
  // Remove entries and keep them in a set
  vtkstd::set<vtkSMProxyManagerEntry> entriesToRemove;
  this->Internals->RemoveTuples(name, entriesToRemove);

  // Notify that some entries have been deleted
  vtkstd::set<vtkSMProxyManagerEntry>::iterator iter = entriesToRemove.begin();
  while(iter != entriesToRemove.end())
    {
    RegisteredProxyInformation info;
    info.Proxy = iter->Proxy;
    info.GroupName = iter->Group.c_str();
    info.ProxyName = iter->Name.c_str();
    info.Type = RegisteredProxyInformation::PROXY;

    this->InvokeEvent(vtkCommand::UnRegisterEvent, &info);
    this->UnMarkProxyAsModified(info.Proxy);

    // Move forward
    iter++;
    }

  // Push new state only if changed occured
  if(entriesToRemove.size() > 0)
    {
    this->TriggerStateUpdate();
    }
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::UnRegisterProxy(vtkSMProxy* proxy)
{
  // Find tuples
  vtkstd::set<vtkSMProxyManagerEntry> tuplesToRemove;
  this->Internals->FindProxyTuples(proxy, tuplesToRemove);

  // Remove tuples
  vtkstd::set<vtkSMProxyManagerEntry>::iterator iter = tuplesToRemove.begin();
  while(iter != tuplesToRemove.end())
    {
    this->UnRegisterProxy(iter->Group.c_str(), iter->Name.c_str(), iter->Proxy);
    iter++;
    }

  // Push new state only if changed occured
  if(tuplesToRemove.size() > 0)
    {
    this->TriggerStateUpdate();
    }
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::RegisterProxy(const char* groupname,
                                      const char* name,
                                      vtkSMProxy* proxy)
{
  if (!proxy)
    {
    return;
    }

  vtkSMProxyManagerProxyListType &proxy_list =
    this->Internals->RegisteredProxyMap[groupname][name];
  if (proxy_list.Contains(proxy))
    {
    return;
    }

  // Add Tuple
  this->Internals->RegisteredProxyTuple.insert(
      vtkSMProxyManagerEntry( groupname, name, proxy ));

  vtkSMProxyManagerProxyInfo* proxyInfo = vtkSMProxyManagerProxyInfo::New();
  proxy_list.push_back(proxyInfo);
  proxyInfo->FastDelete();

  proxyInfo->Proxy = proxy;
  // Add observers to note proxy modification.
  proxyInfo->ModifiedObserverTag = proxy->AddObserver(
    vtkCommand::PropertyModifiedEvent, this->Observer);
  proxyInfo->StateChangedObserverTag = proxy->AddObserver(
    vtkCommand::StateChangedEvent, this->Observer);
  proxyInfo->UpdateObserverTag = proxy->AddObserver(vtkCommand::UpdateEvent,
    this->Observer);
  proxyInfo->UpdateInformationObserverTag = proxy->AddObserver(
    vtkCommand::UpdateInformationEvent, this->Observer);

  // Note, these observer will be removed in the destructor of proxyInfo.

  RegisteredProxyInformation info;
  info.Proxy = proxy;
  info.GroupName = groupname;
  info.ProxyName = name;
  info.Type = RegisteredProxyInformation::PROXY;

  this->InvokeEvent(vtkCommand::RegisterEvent, &info);

  // Update state

  if(proxy->GetLocation() != 0 && !proxy->IsPrototype()) // Not a prototype !!!
    {
    proxy->CreateVTKObjects(); // Make sure an ID has been assigned to it

    // Do not put prototype groups in state
    vtksys::RegularExpression prototypesRe("_prototypes$");
    if (!prototypesRe.find(groupname))
      {
      PXMRegisteredProxyState_ProxyRegistrationInfo *registration =
          this->Internals->State.AddExtension(PXMRegisteredProxyState::registered_proxy);
      registration->set_group(groupname);
      registration->set_name(name);
      registration->set_global_id(proxy->GetGlobalID());

      // Push state for undo/redo
      this->TriggerStateUpdate();
      }
    }
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::UpdateRegisteredProxies(const char* groupname,
  int modified_only /*=1*/)
{
  vtkSMProxyManagerInternals::ProxyGroupType::iterator it =
    this->Internals->RegisteredProxyMap.find(groupname);
  if ( it != this->Internals->RegisteredProxyMap.end() )
    {
    vtkSMProxyManagerProxyMapType::iterator it2 = it->second.begin();
    for (; it2 != it->second.end(); it2++)
      {
      vtkSMProxyManagerProxyListType::iterator it3 = it2->second.begin();
      for (; it3 != it2->second.end(); ++it3)
        {
        // Check is proxy is in the modified set.
        if (!modified_only ||
          this->Internals->ModifiedProxies.find(it3->GetPointer()->Proxy.GetPointer())
          != this->Internals->ModifiedProxies.end())
          {
          it3->GetPointer()->Proxy.GetPointer()->UpdateVTKObjects();
          }
        }
      }
    }
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::UpdateRegisteredProxies(int modified_only /*=1*/)
{
  vtksys::RegularExpression prototypesRe("_prototypes$");

  vtkSMProxyManagerInternals::ProxyGroupType::iterator it =
    this->Internals->RegisteredProxyMap.begin();
  for (; it != this->Internals->RegisteredProxyMap.end(); it++)
    {
    if (prototypesRe.find(it->first))
      {
      // skip the prototypes.
      continue;
      }

    vtkSMProxyManagerProxyMapType::iterator it2 = it->second.begin();
    for (; it2 != it->second.end(); it2++)
      {
      // Check is proxy is in the modified set.
      vtkSMProxyManagerProxyListType::iterator it3 = it2->second.begin();
      for (; it3 != it2->second.end(); ++it3)
        {
        // Check if proxy is in the modified set.
        if (!modified_only ||
          this->Internals->ModifiedProxies.find(it3->GetPointer()->Proxy.GetPointer())
          != this->Internals->ModifiedProxies.end())
          {
          it3->GetPointer()->Proxy.GetPointer()->UpdateVTKObjects();
          }
        }
      }
    }
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::UpdateRegisteredProxiesInOrder(int modified_only/*=1*/)
{
  this->UpdateInputProxies = 1;
  this->UpdateRegisteredProxies(modified_only);
  this->UpdateInputProxies = 0;
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::UpdateProxyInOrder(vtkSMProxy* proxy)
{
  this->UpdateInputProxies = 1;
  proxy->UpdateVTKObjects();
  this->UpdateInputProxies = 0;
}

//---------------------------------------------------------------------------
int vtkSMProxyManager::GetNumberOfLinks()
{
  return this->Internals->RegisteredLinkMap.size();
}

//---------------------------------------------------------------------------
const char* vtkSMProxyManager::GetLinkName(int idx)
{
  int numlinks = this->GetNumberOfLinks();
  if(idx >= numlinks)
    {
    return NULL;
    }
  vtkSMProxyManagerInternals::LinkType::iterator it =
    this->Internals->RegisteredLinkMap.begin();
  for(int i=0; i<idx; i++)
    {
    it++;
    }
  return it->first.c_str();
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::RegisterLink(const char* name, vtkSMLink* link)
{
  vtkSMProxyManagerInternals::LinkType::iterator it =
    this->Internals->RegisteredLinkMap.find(name);
  if (it != this->Internals->RegisteredLinkMap.end())
    {
    vtkWarningMacro("Replacing previously registered link with name " << name);
    }
  this->Internals->RegisteredLinkMap[name] = link;

  RegisteredProxyInformation info;
  info.Proxy = 0;
  info.GroupName = 0;
  info.ProxyName = name;
  info.Type = RegisteredProxyInformation::LINK;
  this->InvokeEvent(vtkCommand::RegisterEvent, &info);
}

//---------------------------------------------------------------------------
vtkSMLink* vtkSMProxyManager::GetRegisteredLink(const char* name)
{
  vtkSMProxyManagerInternals::LinkType::iterator it =
    this->Internals->RegisteredLinkMap.find(name);
  if (it != this->Internals->RegisteredLinkMap.end())
    {
    return it->second.GetPointer();
    }
  return NULL;
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::UnRegisterLink(const char* name)
{
  vtkSMProxyManagerInternals::LinkType::iterator it =
    this->Internals->RegisteredLinkMap.find(name);
  if (it != this->Internals->RegisteredLinkMap.end())
    {
    RegisteredProxyInformation info;
    info.Proxy = 0;
    info.GroupName = 0;
    info.ProxyName = name;
    info.Type = RegisteredProxyInformation::LINK;
    this->Internals->RegisteredLinkMap.erase(it);
    this->InvokeEvent(vtkCommand::UnRegisterEvent, &info);
    }
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::UnRegisterAllLinks()
{
  this->Internals->RegisteredLinkMap.clear();
}


//---------------------------------------------------------------------------
void vtkSMProxyManager::ExecuteEvent(vtkObject* obj, unsigned long event,
  void* data)
{
  // Check source object
  vtkSMProxy* proxy = vtkSMProxy::SafeDownCast(obj);
  vtkSMGlobalPropertiesManager* globalPropertiesManager =
      vtkSMGlobalPropertiesManager::SafeDownCast(obj);

  // Manage ProxyDefinitionManager Events
  if(obj == this->ProxyDefinitionManager)
    {
    vtkSIProxyDefinitionManager::RegisteredDefinitionInformation* defInfo;
    switch(event)
      {
      case vtkCommand::RegisterEvent:
      case vtkCommand::UnRegisterEvent:
         defInfo = reinterpret_cast<
           vtkSIProxyDefinitionManager::RegisteredDefinitionInformation*>(data);
         if(defInfo->CustomDefinition)
           {
           RegisteredProxyInformation info;
           info.Proxy = 0;
           info.GroupName = defInfo->GroupName;
           info.ProxyName = defInfo->ProxyName;
           info.Type = RegisteredProxyInformation::COMPOUND_PROXY_DEFINITION;
           this->InvokeEvent(event, &info);
           }
         break;

      default:
         this->InvokeEvent(event, data);
         break;
      }
    }
  // Manage vtkSMGlobalPropertiesManager to trigger UndoElement
  else if(globalPropertiesManager)
    {
    const char* globalPropertiesManagerName =
        this->GetGlobalPropertiesManagerName(globalPropertiesManager);
    if( globalPropertiesManagerName               &&
        this->GetSession()->GetUndoStackBuilder() &&
        event == vtkSMGlobalPropertiesManager::GlobalPropertyLinkModified)
      {
      vtkSMGlobalPropertiesManager::ModifiedInfo* modifiedInfo;
      modifiedInfo = reinterpret_cast<vtkSMGlobalPropertiesManager::ModifiedInfo*>(data);

      vtkSMGlobalPropertiesLinkUndoElement* undoElem = vtkSMGlobalPropertiesLinkUndoElement::New();
      undoElem->SetLinkState( globalPropertiesManagerName,
                              modifiedInfo->GlobalPropertyName,
                              modifiedInfo->Proxy,
                              modifiedInfo->PropertyName,
                              modifiedInfo->AddLink);
      this->GetSession()->GetUndoStackBuilder()->Add(undoElem);
      undoElem->Delete();
      }
    }
  // Manage proxy modification call back to mark proxy dirty...
  else if (proxy)
    {
    switch (event)
      {
    case vtkCommand::PropertyModifiedEvent:
        {
        // Some property on the proxy has been modified.
        this->MarkProxyAsModified(proxy);
        ModifiedPropertyInformation info;
        info.Proxy = proxy;
        info.PropertyName = reinterpret_cast<const char*>(data);
        if (info.PropertyName)
          {
          this->InvokeEvent(vtkCommand::PropertyModifiedEvent,
            &info);
          }
        }
      break;

    case vtkCommand::StateChangedEvent:
        {
        // I wonder if I need to mark the proxy modified. Cause when state
        // changes, the properties are pushed as well so ideally we should call
        // MarkProxyAsModified() and then UnMarkProxyAsModified() :).
        // this->MarkProxyAsModified(proxy);

        StateChangedInformation info;
        info.Proxy = proxy;
        info.StateChangeElement = reinterpret_cast<vtkPVXMLElement*>(data);
        if (info.StateChangeElement)
          {
          this->InvokeEvent(vtkCommand::StateChangedEvent, &info);
          }
        }
      break;

    case vtkCommand::UpdateInformationEvent:
      this->InvokeEvent(vtkCommand::UpdateInformationEvent, proxy);
      break;

    case vtkCommand::UpdateEvent:
      // Proxy has been updated.
      this->UnMarkProxyAsModified(proxy);
      break;
      }
    }
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::MarkProxyAsModified(vtkSMProxy* proxy)
{
  this->Internals->ModifiedProxies.insert(proxy);
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::UnMarkProxyAsModified(vtkSMProxy* proxy)
{
  vtkSMProxyManagerInternals::SetOfProxies::iterator it =
    this->Internals->ModifiedProxies.find(proxy);
  if (it != this->Internals->ModifiedProxies.end())
    {
    this->Internals->ModifiedProxies.erase(it);
    }
}
//---------------------------------------------------------------------------
int vtkSMProxyManager::AreProxiesModified()
{
  return (this->Internals->ModifiedProxies.size() > 0)? 1: 0;
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::LoadXMLState( const char* filename,
                                      vtkSMStateLoader* loader/*=NULL*/)
{
  vtkPVXMLParser* parser = vtkPVXMLParser::New();
  parser->SetFileName(filename);
  parser->Parse();

  this->LoadXMLState(parser->GetRootElement(), loader);
  parser->Delete();
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::LoadXMLState( vtkPVXMLElement* rootElement,
                                      vtkSMStateLoader* loader/*=NULL*/)
{
  if (!rootElement)
    {
    return;
    }
  vtkSmartPointer<vtkSMStateLoader> spLoader;

  if (!loader)
    {
    spLoader = vtkSmartPointer<vtkSMStateLoader>::New();
    spLoader->SetSession(this->GetSession());
    }
  else
    {
    spLoader = loader;
    }
  if (spLoader->LoadState(rootElement))
    {
    LoadStateInformation info;
    info.RootElement = rootElement;
    info.ProxyLocator = spLoader->GetProxyLocator();
    this->InvokeEvent(vtkCommand::LoadStateEvent, &info);
    }
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::SaveXMLState(const char* filename)
{
  vtkPVXMLElement* rootElement = this->SaveXMLState();
  ofstream os(filename, ios::out);
  rootElement->PrintXML(os, vtkIndent());
  rootElement->Delete();
}

//---------------------------------------------------------------------------
vtkPVXMLElement* vtkSMProxyManager::SaveXMLState()
{
  vtkPVXMLElement* root = vtkPVXMLElement::New();
  root->SetName("GenericParaViewApplication");
  this->AddInternalState(root);

  LoadStateInformation info;
  info.RootElement = root;
  info.ProxyLocator = NULL;
  this->InvokeEvent(vtkCommand::SaveStateEvent, &info);
  return root;
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::CollectReferredProxies(
  vtkSMProxyManagerProxySet& setOfProxies, vtkSMProxy* proxy)
{
  vtkSmartPointer<vtkSMPropertyIterator> iter;
  iter.TakeReference(proxy->NewPropertyIterator());
  for (iter->Begin(); !iter->IsAtEnd(); iter->Next())
    {
    vtkSMProxyProperty* pp = vtkSMProxyProperty::SafeDownCast(
      iter->GetProperty());
    for (unsigned int cc=0; pp && (pp->GetNumberOfProxies() > cc); cc++)
      {
      vtkSMProxy* referredProxy = pp->GetProxy(cc);
      if (referredProxy)
        {
        setOfProxies.insert(referredProxy);
        this->CollectReferredProxies(setOfProxies, referredProxy);
        }
      }
    }
}


//---------------------------------------------------------------------------
vtkPVXMLElement* vtkSMProxyManager::AddInternalState(vtkPVXMLElement *parentElem)
{
  vtkPVXMLElement* rootElement = vtkPVXMLElement::New();
  rootElement->SetName("ServerManagerState");

  // Set version number on the state element.
  vtksys_ios::ostringstream version_string;
  version_string << this->GetVersionMajor() << "."
    << this->GetVersionMinor() << "." << this->GetVersionPatch();
  rootElement->AddAttribute("version", version_string.str().c_str());


  vtkstd::set<vtkSMProxy*> visited_proxies; // set of proxies already added.

  // First save the state of all proxies
  vtkSMProxyManagerInternals::ProxyGroupType::iterator it =
    this->Internals->RegisteredProxyMap.begin();
  for (; it != this->Internals->RegisteredProxyMap.end(); it++)
    {
    vtkSMProxyManagerProxyMapType::iterator it2 =
      it->second.begin();

    // Do not save the state of prototypes.
    const char* protstr = "_prototypes";
    const char* colname = it->first.c_str();
    int do_group = 1;
    if (strlen(colname) > strlen(protstr))
      {
      const char* newstr = colname + strlen(colname) -
        strlen(protstr);
      if (strcmp(newstr, protstr) == 0)
        {
        do_group = 0;
        }
      }
    else if ( colname[0] == '_' )
      {
      do_group = 0;
      }
    if (!do_group)
      {
      continue;
      }

    // save the states of all proxies in this group.
    for (; it2 != it->second.end(); it2++)
      {
      vtkSMProxyManagerProxyListType::iterator it3
        = it2->second.begin();
      for (; it3 != it2->second.end(); ++it3)
        {
        if (visited_proxies.find(it3->GetPointer()->Proxy.GetPointer())
          != visited_proxies.end())
          {
          // proxy has been saved.
          continue;
          }
        it3->GetPointer()->Proxy.GetPointer()->SaveXMLState(rootElement);
        visited_proxies.insert(it3->GetPointer()->Proxy.GetPointer());
        }
      }
    }

  // Save the proxy collections. This is done seprately because
  // one proxy can be in more than one group.
  it = this->Internals->RegisteredProxyMap.begin();
  for (; it != this->Internals->RegisteredProxyMap.end(); it++)
    {
    // Do not save the state of prototypes.
    const char* protstr = "_prototypes";
    int do_group = 1;
    if (strlen(it->first.c_str()) > strlen(protstr))
      {
      const char* newstr = it->first.c_str() + strlen(it->first.c_str()) -
        strlen(protstr);
      if (strcmp(newstr, protstr) == 0)
        {
        do_group = 0;
        }
      }
    if (do_group)
      {
      vtkPVXMLElement* collectionElement = vtkPVXMLElement::New();
      collectionElement->SetName("ProxyCollection");
      collectionElement->AddAttribute("name", it->first.c_str());
      vtkSMProxyManagerProxyMapType::iterator it2 =
        it->second.begin();
      bool some_proxy_added = false;
      for (; it2 != it->second.end(); it2++)
        {
        vtkSMProxyManagerProxyListType::iterator it3 =
          it2->second.begin();
        for (; it3 != it2->second.end(); ++it3)
          {
          if (visited_proxies.find(it3->GetPointer()->Proxy.GetPointer()) != visited_proxies.end())
            {
            vtkPVXMLElement* itemElement = vtkPVXMLElement::New();
            itemElement->SetName("Item");
            itemElement->AddAttribute("id",
              it3->GetPointer()->Proxy->GetGlobalID());
            itemElement->AddAttribute("name", it2->first.c_str());
            collectionElement->AddNestedElement(itemElement);
            itemElement->Delete();
            some_proxy_added = true;
            }
          }
        }
      // Avoid addition of empty groups.
      if (some_proxy_added)
        {
        rootElement->AddNestedElement(collectionElement);
        }
      collectionElement->Delete();
      }
    }

  // TODO: Save definitions for only referred compound proxy definitions
  // when saving state for subset of proxies.
  vtkPVXMLElement* defs = vtkPVXMLElement::New();
  defs->SetName("CustomProxyDefinitions");
  this->SaveCustomProxyDefinitions(defs);
  rootElement->AddNestedElement(defs);
  defs->Delete();

  // Save links
  vtkPVXMLElement* links = vtkPVXMLElement::New();
  links->SetName("Links");
  this->SaveRegisteredLinks(links);
  rootElement->AddNestedElement(links);
  links->Delete();

  vtkPVXMLElement* globalProps = vtkPVXMLElement::New();
  globalProps->SetName("GlobalPropertiesManagers");
  this->SaveGlobalPropertiesManagers(globalProps);
  rootElement->AddNestedElement(globalProps);
  globalProps->Delete();

  if(parentElem)
    {
    parentElem->AddNestedElement(rootElement);
    rootElement->FastDelete();
    }

  return rootElement;
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::UnRegisterCustomProxyDefinitions()
{
  assert(this->ProxyDefinitionManager != 0);
  this->ProxyDefinitionManager->ClearCustomProxyDefinitions();
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::UnRegisterCustomProxyDefinition(
  const char* group, const char* name)
{
  assert(this->ProxyDefinitionManager != 0);
  this->ProxyDefinitionManager->RemoveCustomProxyDefinition(group, name);
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::RegisterCustomProxyDefinition(
  const char* group, const char* name, vtkPVXMLElement* top)
{
  assert(this->ProxyDefinitionManager != 0);
  this->ProxyDefinitionManager->AddCustomProxyDefinition(group, name, top);
}

//---------------------------------------------------------------------------
// Does not raise an error if definition is not found.
vtkPVXMLElement* vtkSMProxyManager::GetProxyDefinition(
  const char* group, const char* name)
{
  if (!name || !group)
    {
    return 0;
    }


  assert(this->ProxyDefinitionManager != 0);
  return this->ProxyDefinitionManager->GetProxyDefinition(group, name, false);
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::LoadCustomProxyDefinitions(vtkPVXMLElement* root)
{
  assert(this->ProxyDefinitionManager != 0);
  this->ProxyDefinitionManager->LoadCustomProxyDefinitions(root);
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::LoadCustomProxyDefinitions(const char* filename)
{
  assert(this->ProxyDefinitionManager != 0);
  vtkPVXMLParser* parser = vtkPVXMLParser::New();
  parser->SetFileName(filename);
  if (!parser->Parse())
    {
    vtkErrorMacro("Failed to parse file : " << filename);
    return;
    }
  this->ProxyDefinitionManager->LoadCustomProxyDefinitions(parser->GetRootElement());
  parser->Delete();
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::SaveCustomProxyDefinitions(
  vtkPVXMLElement* rootElement)
{
  assert(this->ProxyDefinitionManager != 0);
  this->ProxyDefinitionManager->SaveCustomProxyDefinitions(rootElement);
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::SaveRegisteredLinks(vtkPVXMLElement* rootElement)
{
  vtkSMProxyManagerInternals::LinkType::iterator it =
    this->Internals->RegisteredLinkMap.begin();
  for (; it != this->Internals->RegisteredLinkMap.end(); ++it)
    {
    it->second.GetPointer()->SaveState(it->first.c_str(), rootElement);
    }
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::SaveGlobalPropertiesManagers(vtkPVXMLElement* root)
{
  vtkSMProxyManagerInternals::GlobalPropertiesManagersType::iterator iter;
  for (iter = this->Internals->GlobalPropertiesManagers.begin();
    iter != this->Internals->GlobalPropertiesManagers.end(); ++iter)
    {
    vtkPVXMLElement* elem = iter->second->SaveLinkState(root);
    if (elem)
      {
      elem->AddAttribute("name", iter->first.c_str());
      }
    }
}

//---------------------------------------------------------------------------
vtkPVXMLElement* vtkSMProxyManager::GetProxyHints(
  const char* groupName, const char* proxyName)
{
  if (!groupName || !proxyName)
    {
    return 0;
    }

  vtkSMProxy* proxy = this->GetPrototypeProxy(groupName, proxyName);
  return proxy? proxy->GetHints() : NULL;
}

//---------------------------------------------------------------------------
vtkPVXMLElement* vtkSMProxyManager::GetPropertyHints(
  const char* groupName, const char* proxyName, const char* propertyName)
{
  if (!groupName || !proxyName || !propertyName)
    {
    return 0;
    }

  vtkSMProxy* proxy = this->GetPrototypeProxy(groupName, proxyName);
  if (proxy)
    {
    vtkSMProperty* prop = proxy->GetProperty(propertyName);
    if (prop)
      {
      return prop->GetHints();
      }
    }
  return 0;
}

//---------------------------------------------------------------------------
vtkSMProxySelectionModel* vtkSMProxyManager::GetSelectionModel(
  const char* name)
{
  if(!this->Session)
    {
    return NULL;
    }

  vtkSMProxyManagerInternals::SelectionModelsType::iterator iter =
    this->Internals->SelectionModels.find(name);
  if (iter == this->Internals->SelectionModels.end())
    {
    vtkNew<vtkSMProxySelectionModel> model;
    model->SetSession(this->Session);
    this->Internals->SelectionModels[name] = model.GetPointer();
    return model.GetPointer();
    }

  return iter->second;
}
//---------------------------------------------------------------------------
void vtkSMProxyManager::SetGlobalPropertiesManager(const char* name,
    vtkSMGlobalPropertiesManager* mgr)
{
  vtkSMGlobalPropertiesManager* old_mgr = this->GetGlobalPropertiesManager(name);
  if (old_mgr == mgr)
    {
    return;
    }
  this->RemoveGlobalPropertiesManager(name);
  this->Internals->GlobalPropertiesManagers[name] = mgr;
  this->Internals->GlobalPropertiesManagersCallBackID[name] =
      mgr->AddObserver(vtkSMGlobalPropertiesManager::GlobalPropertyLinkModified, this->Observer);

  RegisteredProxyInformation info;
  info.Proxy = mgr;
  info.GroupName = NULL;
  info.ProxyName = name;
  info.Type = RegisteredProxyInformation::GLOBAL_PROPERTIES_MANAGER;
  this->InvokeEvent(vtkCommand::RegisterEvent, &info);
}

//---------------------------------------------------------------------------
const char* vtkSMProxyManager::GetGlobalPropertiesManagerName(
  vtkSMGlobalPropertiesManager* mgr)
{
  vtkSMProxyManagerInternals::GlobalPropertiesManagersType::iterator iter;
  for (iter = this->Internals->GlobalPropertiesManagers.begin();
    iter != this->Internals->GlobalPropertiesManagers.end(); ++iter)
    {
    if (iter->second == mgr)
      {
      return iter->first.c_str();
      }
    }
  return 0;
}

//---------------------------------------------------------------------------
vtkSMGlobalPropertiesManager* vtkSMProxyManager::GetGlobalPropertiesManager(
  const char* name)
{
  return this->Internals->GlobalPropertiesManagers[name].GetPointer();
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::RemoveGlobalPropertiesManager(const char* name)
{
  vtkSMGlobalPropertiesManager* gm = this->GetGlobalPropertiesManager(name);
  if (gm)
    {
    gm->RemoveObserver(this->Internals->GlobalPropertiesManagersCallBackID[name]);
    RegisteredProxyInformation info;
    info.Proxy = gm;
    info.GroupName = NULL;
    info.ProxyName = name;
    info.Type = RegisteredProxyInformation::GLOBAL_PROPERTIES_MANAGER;
    this->InvokeEvent(vtkCommand::UnRegisterEvent, &info);
    }
  this->Internals->GlobalPropertiesManagers.erase(name);
}

//---------------------------------------------------------------------------
unsigned int vtkSMProxyManager::GetNumberOfGlobalPropertiesManagers()
{
  return static_cast<unsigned int>(
    this->Internals->GlobalPropertiesManagers.size());
}

//---------------------------------------------------------------------------
vtkSMGlobalPropertiesManager* vtkSMProxyManager::GetGlobalPropertiesManager(
  unsigned int index)
{
  unsigned int cur =0;
  vtkSMProxyManagerInternals::GlobalPropertiesManagersType::iterator iter;
  for (iter = this->Internals->GlobalPropertiesManagers.begin();
    iter != this->Internals->GlobalPropertiesManagers.end(); ++iter, ++cur)
    {
    if (cur == index)
      {
      return iter->second;
      }
    }

  return NULL;
}

//---------------------------------------------------------------------------
bool vtkSMProxyManager::LoadConfigurationXML(const char* xml)
{
  assert(this->ProxyDefinitionManager != 0);
  return this->ProxyDefinitionManager->LoadConfigurationXMLFromString(xml);
}

//---------------------------------------------------------------------------
void vtkSMProxyManager::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent <<  "UpdateInputProxies: " <<  this->UpdateInputProxies << endl;
}
//---------------------------------------------------------------------------
const vtkSMMessage* vtkSMProxyManager::GetFullState()
{
  if(!this->Internals->State.has_global_id())
    {
    this->Internals->State.set_global_id(vtkSMProxyManager::GetReservedGlobalID());
    this->Internals->State.set_location(vtkProcessModule::PROCESS_DATA_SERVER);
    this->Internals->State.SetExtension(DefinitionHeader::client_class, "");
    this->Internals->State.SetExtension(DefinitionHeader::server_class, "vtkSIObject");
    this->Internals->State.SetExtension(ProxyState::xml_group, "");
    this->Internals->State.SetExtension(ProxyState::xml_name, "");
    }

  return &this->Internals->State;
}
//---------------------------------------------------------------------------
void vtkSMProxyManager::LoadState(const vtkSMMessage* msg, vtkSMProxyLocator* locator)
{
  // Need to compute differences and just call Register/UnRegister for those items
  vtkstd::set<vtkSMProxyManagerEntry> tuplesToUnregister;
  vtkstd::set<vtkSMProxyManagerEntry> tuplesToRegister;
  vtkstd::set<vtkSMProxyManagerEntry>::iterator iter;

  // Fill delta sets
  this->Internals->ComputeDelta(msg, locator, tuplesToRegister, tuplesToUnregister);

  // Register new ones
  iter = tuplesToRegister.begin();
  while( iter != tuplesToRegister.end() )
    {
    this->RegisterProxy(iter->Group.c_str(), iter->Name.c_str(), iter->Proxy);
    iter++;
    }

  // Unregister old ones
  iter = tuplesToUnregister.begin();
  while( iter != tuplesToUnregister.end() )
    {
    this->UnRegisterProxy(iter->Group.c_str(), iter->Name.c_str(), iter->Proxy);
    iter++;
    }

  // Manage ProxySelectionModel state
  for(int i = 0,
      size = msg->ExtensionSize(PXMRegisteredSelectionModelState::registered_selection_model);
      i < size && this->Session;
      i++)
    {
    vtkTypeUInt32 remoteObjectId =
        msg->GetExtension(PXMRegisteredSelectionModelState::registered_selection_model, i).global_id();
    const char* name =
        msg->GetExtension(PXMRegisteredSelectionModelState::registered_selection_model, i).name().c_str();

    vtkSMProxySelectionModel* model = this->GetSelectionModel(name);
    if(!model->HasGlobalID())
      {
      vtkSMMessage msg;
      msg.set_global_id(remoteObjectId);
      msg.set_location(vtkPVSession::DATA_SERVER_ROOT);
      this->Session->PullState(&msg);

      model->LoadState(&msg, locator);

      this->Internals->UpdateProxySelectionModelState();
      this->TriggerStateUpdate();
      }
    }
}

//---------------------------------------------------------------------------
bool vtkSMProxyManager::HasDefinition( const char* groupName,
                                       const char* proxyName )
{
  return this->ProxyDefinitionManager &&
      this->ProxyDefinitionManager->HasDefinition(groupName, proxyName);
}
//---------------------------------------------------------------------------
bool vtkSMProxyManager::IsStateUpdateNotificationEnabled()
{
  return this->StateUpdateNotification;
}
//---------------------------------------------------------------------------
void vtkSMProxyManager::DisableStateUpdateNotification()
{
  this->StateUpdateNotification = false;
}
//---------------------------------------------------------------------------
void vtkSMProxyManager::EnableStateUpdateNotification()
{
  this->StateUpdateNotification = true;
}
//---------------------------------------------------------------------------
void vtkSMProxyManager::TriggerStateUpdate()
{
  if(this->PipelineState && this->StateUpdateNotification)
    {
    this->Internals->UpdateProxySelectionModelState();
    this->PipelineState->ValidateState();
    }
}
