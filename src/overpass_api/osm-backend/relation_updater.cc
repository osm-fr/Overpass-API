/** Copyright 2008, 2009, 2010, 2011, 2012 Roland Olbricht
*
* This file is part of Overpass_API.
*
* Overpass_API is free software: you can redistribute it and/or modify
* it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Overpass_API is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Overpass_API.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <vector>

#include "../../template_db/block_backend.h"
#include "../../template_db/random_file.h"
#include "../core/datatypes.h"
#include "../core/settings.h"
#include "meta_updater.h"
#include "relation_updater.h"
#include "tags_updater.h"

using namespace std;


Update_Relation_Logger::~Update_Relation_Logger()
{
  for (map< Relation::Id_Type, pair< Relation, OSM_Element_Metadata* > >::const_iterator it = insert.begin();
      it != insert.end(); ++it)
  {
    if (it->second.second)
      delete it->second.second;
  }
  for (map< Relation::Id_Type, pair< Relation, OSM_Element_Metadata* > >::const_iterator it = keep.begin();
      it != keep.end(); ++it)
  {
    if (it->second.second)
      delete it->second.second;
  }
  for (map< Relation::Id_Type, pair< Relation, OSM_Element_Metadata* > >::const_iterator it = erase.begin();
      it != erase.end(); ++it)
  {
    if (it->second.second)
      delete it->second.second;
  }
}


Relation_Updater::Relation_Updater(Transaction& transaction_, meta_modes meta_)
  : update_counter(0), transaction(&transaction_),
    external_transaction(true),
    max_role_id(0), max_written_role_id(0), meta(meta_)
{}

Relation_Updater::Relation_Updater(string db_dir_, meta_modes meta_)
  : update_counter(0), transaction(0),
    external_transaction(false),
    max_role_id(0), max_written_role_id(0), db_dir(db_dir_), meta(meta_)
{}


void Relation_Updater::load_roles()
{
  if (!external_transaction)
    transaction = new Nonsynced_Transaction(true, false, db_dir, "");
    
  Block_Backend< Uint32_Index, String_Object > roles_db
      (transaction->data_index(osm_base_settings().RELATION_ROLES));
  for (Block_Backend< Uint32_Index, String_Object >::Flat_Iterator
      it(roles_db.flat_begin()); !(it == roles_db.flat_end()); ++it)
  {
    role_ids[it.object().val()] = it.index().val();
    if (max_role_id <= it.index().val())
      max_role_id = it.index().val()+1;
  }
  max_written_role_id = max_role_id;
    
  if (!external_transaction)
    delete transaction;
}


uint32 Relation_Updater::get_role_id(const string& s)
{
  if (max_role_id == 0)
    load_roles();
  map< string, uint32 >::const_iterator it(role_ids.find(s));
  if (it != role_ids.end())
    return it->second;
  role_ids[s] = max_role_id;
  ++max_role_id;
  return (max_role_id - 1);
}


// TODO: temporary helper function for update_logger
void tell_update_logger_insertions
    (const typename Data_By_Id< Relation_Skeleton >::Entry& entry, Update_Relation_Logger* update_logger)
{
  if (update_logger)
  {
    Relation relation(entry.elem.id.val(), entry.idx.val(), entry.elem.members);
    relation.tags = entry.tags;
    update_logger->insertion(relation);
  }
}


std::map< Uint31_Index, std::set< Relation_Skeleton > > get_implicitly_moved_skeletons
    (const std::map< Uint31_Index, std::set< Node_Skeleton > >& attic_nodes,
     const std::map< Uint31_Index, std::set< Way_Skeleton > >& attic_ways,
     const std::map< Uint31_Index, std::set< Relation_Skeleton > >& already_known_skeletons,
     Transaction& transaction, const File_Properties& file_properties)
{
  std::set< Uint31_Index > member_req;
  for (std::map< Uint31_Index, std::set< Node_Skeleton > >::const_iterator
      it = attic_nodes.begin(); it != attic_nodes.end(); ++it)
    member_req.insert(it->first);
  for (std::map< Uint31_Index, std::set< Way_Skeleton > >::const_iterator
      it = attic_ways.begin(); it != attic_ways.end(); ++it)
    member_req.insert(it->first);
  std::set< Uint31_Index > req = calc_parents(member_req);
  
  std::vector< Node_Skeleton::Id_Type > node_ids;
  for (std::map< Uint31_Index, std::set< Node_Skeleton > >::const_iterator
      it = attic_nodes.begin(); it != attic_nodes.end(); ++it)
  {
    for (std::set< Node_Skeleton >::const_iterator nit = it->second.begin(); nit != it->second.end(); ++nit)
      node_ids.push_back(nit->id);
  }
  std::sort(node_ids.begin(), node_ids.end());
  node_ids.erase(std::unique(node_ids.begin(), node_ids.end()), node_ids.end());
  
  std::vector< Way_Skeleton::Id_Type > way_ids;
  for (std::map< Uint31_Index, std::set< Way_Skeleton > >::const_iterator
      it = attic_ways.begin(); it != attic_ways.end(); ++it)
  {
    for (std::set< Way_Skeleton >::const_iterator nit = it->second.begin(); nit != it->second.end(); ++nit)
      way_ids.push_back(nit->id);
  }
  std::sort(way_ids.begin(), way_ids.end());
  way_ids.erase(std::unique(way_ids.begin(), way_ids.end()), way_ids.end());
  
  std::vector< Relation_Skeleton::Id_Type > known_relation_ids;
  for (std::map< Uint31_Index, std::set< Relation_Skeleton > >::const_iterator
      it = already_known_skeletons.begin(); it != already_known_skeletons.end(); ++it)
  {
    for (std::set< Relation_Skeleton >::const_iterator wit = it->second.begin(); wit != it->second.end(); ++wit)
      known_relation_ids.push_back(wit->id);
  }
  std::sort(known_relation_ids.begin(), known_relation_ids.end());
  known_relation_ids.erase(std::unique(known_relation_ids.begin(), known_relation_ids.end()), known_relation_ids.end());
  
  std::map< Uint31_Index, std::set< Relation_Skeleton > > result;
  
  Block_Backend< Uint31_Index, Relation_Skeleton > db(transaction.data_index(&file_properties));
  for (Block_Backend< Uint31_Index, Relation_Skeleton >::Discrete_Iterator
      it(db.discrete_begin(req.begin(), req.end())); !(it == db.discrete_end()); ++it)
  {
    if (binary_search(known_relation_ids.begin(), known_relation_ids.end(), it.object().id))
      continue;
    for (vector< Relation_Entry >::const_iterator nit = it.object().members.begin();
         nit != it.object().members.end(); ++nit)
    {
      if (nit->type == Relation_Entry::NODE)
      {
        if (binary_search(node_ids.begin(), node_ids.end(), Node_Skeleton::Id_Type(nit->ref.val())))
        {
          result[it.index()].insert(it.object());
          break;
        }
      }
      else if (nit->type == Relation_Entry::WAY)
      {
        if (binary_search(way_ids.begin(), way_ids.end(), Way_Skeleton::Id_Type(nit->ref.val())))
        {
          result[it.index()].insert(it.object());
          break;
        }
      }
    }
  }

  return result;
}


/* Adds to attic_skeletons and new_skeletons all those relations that have moved just because
   a node in these relations has moved.
   We assert that every node id that appears in a relation in existing_skeletons has its Quad_Coord
   in new_node_idx_by_id. */
void new_implicit_skeletons
    (const std::map< Node_Skeleton::Id_Type, Quad_Coord >& new_node_idx_by_id,
     const std::map< Way_Skeleton::Id_Type, Uint31_Index >& new_way_idx_by_id,
     const std::map< Uint31_Index, std::set< Relation_Skeleton > >& existing_skeletons,
     bool record_minuscule_moves,
     std::map< Uint31_Index, std::set< Relation_Skeleton > >& attic_skeletons,
     std::map< Uint31_Index, std::set< Relation_Skeleton > >& new_skeletons,
     vector< pair< Relation::Id_Type, Uint31_Index > >& moved_relations,
     Update_Relation_Logger* update_logger)
{
  for (std::map< Uint31_Index, std::set< Relation_Skeleton > >::const_iterator it = existing_skeletons.begin();
       it != existing_skeletons.end(); ++it)
  {
    for (std::set< Relation_Skeleton >::const_iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2)
      attic_skeletons[it->first].insert(*it2);
  }

  for (std::map< Uint31_Index, std::set< Relation_Skeleton > >::const_iterator it = existing_skeletons.begin();
       it != existing_skeletons.end(); ++it)
  {
    for (std::set< Relation_Skeleton >::const_iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2)
    {
      vector< uint32 > member_idxs;
      for (vector< Relation_Entry >::const_iterator nit = it2->members.begin();
           nit != it2->members.end(); ++nit)
      {
        if (nit->type == Relation_Entry::NODE)
        {
          std::map< Node_Skeleton::Id_Type, Quad_Coord >::const_iterator it2
              = new_node_idx_by_id.find(Node_Skeleton::Id_Type(nit->ref.val()));
          if (it2 != new_node_idx_by_id.end())
            member_idxs.push_back(it2->second.ll_upper);
        }
        else if (nit->type == Relation_Entry::WAY)
        {
          std::map< Way_Skeleton::Id_Type, Uint31_Index >::const_iterator it2
              = new_way_idx_by_id.find(Way_Skeleton::Id_Type(nit->ref.val()));
          if (it2 != new_way_idx_by_id.end())
            member_idxs.push_back(it2->second.val());
        }
      }
    
      Uint31_Index index = Relation::calc_index(member_idxs);
      if (member_idxs.empty())
        index = 0xff;
      
      Relation_Skeleton new_skeleton = *it2;
      new_skeleton.node_idxs.clear();
      new_skeleton.way_idxs.clear();
      
      if (Relation::indicates_geometry(index))
      {    
        for (vector< Relation_Entry >::const_iterator nit = it2->members.begin();
             nit != it2->members.end(); ++nit)
        {
          if (nit->type == Relation_Entry::NODE)
          {
            std::map< Node_Skeleton::Id_Type, Quad_Coord >::const_iterator it2
                = new_node_idx_by_id.find(Node_Skeleton::Id_Type(nit->ref.val()));
            if (it2 != new_node_idx_by_id.end())
              new_skeleton.node_idxs.push_back(it2->second.ll_upper);
          }
          else if (nit->type == Relation_Entry::WAY)
          {
            std::map< Way_Skeleton::Id_Type, Uint31_Index >::const_iterator it2
                = new_way_idx_by_id.find(Way_Skeleton::Id_Type(nit->ref.val()));
            if (it2 != new_way_idx_by_id.end())
              new_skeleton.way_idxs.push_back(it2->second);
          }
        }
        
        new_skeletons[index].insert(new_skeleton);
      }
      else
        new_skeletons[index].insert(new_skeleton);
      
      if (!(index == it->first))
        moved_relations.push_back(std::make_pair(it2->id, it->first));
    }
  }
}


inline std::map< Way_Skeleton::Id_Type, Uint31_Index > dictionary_from_skeletons
    (const std::map< Uint31_Index, std::set< Way_Skeleton > >& new_way_skeletons)
{
  std::map< Way_Skeleton::Id_Type, Uint31_Index > result;
  
  for (std::map< Uint31_Index, std::set< Way_Skeleton > >::const_iterator
      it = new_way_skeletons.begin(); it != new_way_skeletons.end(); ++it)
  {
    for (std::set< Way_Skeleton >::const_iterator nit = it->second.begin(); nit != it->second.end(); ++nit)
      result.insert(make_pair(nit->id, it->first.val()));
  }
  
  return result;
}


void lookup_missing_nodes
    (std::map< Node_Skeleton::Id_Type, Quad_Coord >& new_node_idx_by_id,
     const Data_By_Id< Relation_Skeleton >& new_data,
     const std::map< Uint31_Index, std::set< Relation_Skeleton > >& known_skeletons,
     Transaction& transaction)
{
  std::vector< Node_Skeleton::Id_Type > missing_ids;
  
  for (std::vector< Data_By_Id< Relation_Skeleton >::Entry >::const_iterator
      it = new_data.data.begin(); it != new_data.data.end(); ++it)
  {
    if (it->idx == 0u)
      // We don't touch deleted objects
      continue;
    
    vector< uint32 > nd_idxs;
    for (vector< Relation_Entry >::const_iterator nit = it->elem.members.begin();
         nit != it->elem.members.end(); ++nit)
    {
      if (nit->type == Relation_Entry::NODE &&
          new_node_idx_by_id.find(nit->ref) == new_node_idx_by_id.end())
        missing_ids.push_back(nit->ref);
    }
  }
  
  for (std::map< Uint31_Index, std::set< Relation_Skeleton > >::const_iterator it = known_skeletons.begin();
       it != known_skeletons.end(); ++it)
  {
    for (std::set< Relation_Skeleton >::const_iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2)
    {
      for (vector< Relation_Entry >::const_iterator nit = it2->members.begin();
           nit != it2->members.end(); ++nit)
      {
        if (nit->type == Relation_Entry::NODE &&
            new_node_idx_by_id.find(nit->ref) == new_node_idx_by_id.end())
          missing_ids.push_back(nit->ref);
      }
    }
  }
  
  std::sort(missing_ids.begin(), missing_ids.end());
  missing_ids.erase(std::unique(missing_ids.begin(), missing_ids.end()), missing_ids.end());
  
  // Collect all data of existing id indexes
  std::vector< std::pair< Node_Skeleton::Id_Type, Uint31_Index > > existing_map_positions
      = get_existing_map_positions(missing_ids, transaction, *osm_base_settings().NODES);
  
  // Collect all data of existing skeletons
  std::map< Uint31_Index, std::set< Node_Skeleton > > existing_skeletons
      = get_existing_skeletons< Node_Skeleton >
      (existing_map_positions, transaction, *osm_base_settings().NODES);
      
  for (std::map< Uint31_Index, std::set< Node_Skeleton > >::const_iterator it = existing_skeletons.begin();
       it != existing_skeletons.end(); ++it)
  {
    for (std::set< Node_Skeleton >::const_iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2)
      new_node_idx_by_id.insert(make_pair(it2->id, Quad_Coord(it->first.val(), it2->ll_lower)));
  }
}


void lookup_missing_ways
    (std::map< Way_Skeleton::Id_Type, Uint31_Index >& new_way_idx_by_id,
     const Data_By_Id< Relation_Skeleton >& new_data,
     const std::map< Uint31_Index, std::set< Relation_Skeleton > >& known_skeletons,
     Transaction& transaction)
{
  std::vector< Way_Skeleton::Id_Type > missing_ids;
  
  for (std::vector< Data_By_Id< Relation_Skeleton >::Entry >::const_iterator
      it = new_data.data.begin(); it != new_data.data.end(); ++it)
  {
    if (it->idx == 0u)
      // We don't touch deleted objects
      continue;
    
    vector< uint32 > nd_idxs;
    for (vector< Relation_Entry >::const_iterator nit = it->elem.members.begin();
         nit != it->elem.members.end(); ++nit)
    {
      if (nit->type == Relation_Entry::WAY &&
          new_way_idx_by_id.find(Way_Skeleton::Id_Type(nit->ref.val())) == new_way_idx_by_id.end())
        missing_ids.push_back(Way_Skeleton::Id_Type(nit->ref.val()));
    }
  }
  
  for (std::map< Uint31_Index, std::set< Relation_Skeleton > >::const_iterator it = known_skeletons.begin();
       it != known_skeletons.end(); ++it)
  {
    for (std::set< Relation_Skeleton >::const_iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2)
    {
      for (vector< Relation_Entry >::const_iterator nit = it2->members.begin();
           nit != it2->members.end(); ++nit)
      {
        if (nit->type == Relation_Entry::WAY &&
            new_way_idx_by_id.find(Way_Skeleton::Id_Type(nit->ref.val())) == new_way_idx_by_id.end())
          missing_ids.push_back(Way_Skeleton::Id_Type(nit->ref.val()));
      }
    }
  }
  
  std::sort(missing_ids.begin(), missing_ids.end());
  missing_ids.erase(std::unique(missing_ids.begin(), missing_ids.end()), missing_ids.end());
  
  // Collect all data of existing id indexes
  std::vector< std::pair< Way_Skeleton::Id_Type, Uint31_Index > > existing_map_positions
      = get_existing_map_positions(missing_ids, transaction, *osm_base_settings().WAYS);
  
  // Collect all data of existing skeletons
  std::map< Uint31_Index, std::set< Way_Skeleton > > existing_skeletons
      = get_existing_skeletons< Way_Skeleton >
      (existing_map_positions, transaction, *osm_base_settings().WAYS);
      
  for (std::map< Uint31_Index, std::set< Way_Skeleton > >::const_iterator it = existing_skeletons.begin();
       it != existing_skeletons.end(); ++it)
  {
    for (std::set< Way_Skeleton >::const_iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2)
      new_way_idx_by_id.insert(make_pair(it2->id, it->first.val()));
  }
}


/* We assert that every node id that appears in a relation in existing_skeletons has its Quad_Coord
   in new_node_idx_by_id. */
void compute_geometry
    (const std::map< Node_Skeleton::Id_Type, Quad_Coord >& new_node_idx_by_id,
     const std::map< Way_Skeleton::Id_Type, Uint31_Index >& new_way_idx_by_id,
     Data_By_Id< Relation_Skeleton >& new_data)
{
  for (std::vector< Data_By_Id< Relation_Skeleton >::Entry >::iterator
      it = new_data.data.begin(); it != new_data.data.end(); ++it)
  {
    if (it->idx == 0u)
      // We don't touch deleted objects
      continue;
    
    vector< uint32 > member_idxs;
    for (vector< Relation_Entry >::const_iterator nit = it->elem.members.begin();
         nit != it->elem.members.end(); ++nit)
    {
      if (nit->type == Relation_Entry::NODE)
      {
        std::map< Node_Skeleton::Id_Type, Quad_Coord >::const_iterator it2
            = new_node_idx_by_id.find(Node_Skeleton::Id_Type(nit->ref.val()));
        if (it2 != new_node_idx_by_id.end())
          member_idxs.push_back(it2->second.ll_upper);
      }
      else if (nit->type == Relation_Entry::WAY)
      {
        std::map< Way_Skeleton::Id_Type, Uint31_Index >::const_iterator it2
            = new_way_idx_by_id.find(Way_Skeleton::Id_Type(nit->ref.val()));
        if (it2 != new_way_idx_by_id.end())
          member_idxs.push_back(it2->second.val());
      }
    }
    
    Uint31_Index index = Relation::calc_index(member_idxs);
      
    it->elem.node_idxs.clear();
    it->elem.way_idxs.clear();
    
    if (Relation::indicates_geometry(index))
    {    
      for (vector< Relation_Entry >::const_iterator nit = it->elem.members.begin();
           nit != it->elem.members.end(); ++nit)
      {
        if (nit->type == Relation_Entry::NODE)
        {
          std::map< Node_Skeleton::Id_Type, Quad_Coord >::const_iterator it2
              = new_node_idx_by_id.find(Node_Skeleton::Id_Type(nit->ref.val()));
          if (it2 != new_node_idx_by_id.end())
            it->elem.node_idxs.push_back(it2->second.ll_upper);
        }
        else if (nit->type == Relation_Entry::WAY)
        {
          std::map< Way_Skeleton::Id_Type, Uint31_Index >::const_iterator it2
              = new_way_idx_by_id.find(Way_Skeleton::Id_Type(nit->ref.val()));
          if (it2 != new_way_idx_by_id.end())
            it->elem.way_idxs.push_back(it2->second);
        }
      }
    }
    
    if (!member_idxs.empty())
      it->idx = index;
  }
}


void Relation_Updater::update(Osm_Backend_Callback* callback,
              Update_Relation_Logger* update_logger,
              const std::map< Uint31_Index, std::set< Node_Skeleton > >& new_node_skeletons,
              const std::map< Uint31_Index, std::set< Node_Skeleton > >& attic_node_skeletons,
              const std::map< Uint31_Index, std::set< Attic< Node_Skeleton > > >& new_attic_node_skeletons,
              const std::map< Uint31_Index, std::set< Way_Skeleton > >& new_way_skeletons,
              const std::map< Uint31_Index, std::set< Way_Skeleton > >& attic_way_skeletons,
              const std::map< Uint31_Index, std::set< Attic< Way_Skeleton > > >& new_attic_way_skeletons)
{
  if (!external_transaction)
    transaction = new Nonsynced_Transaction(true, false, db_dir, "");
  
  // Prepare collecting all data of existing skeletons
  std::sort(new_data.data.begin(), new_data.data.end());
  std::vector< Relation_Skeleton::Id_Type > ids_to_update_ = ids_to_update(new_data);
  
  // Collect all data of existing id indexes
  std::vector< std::pair< Relation_Skeleton::Id_Type, Uint31_Index > > existing_map_positions
      = get_existing_map_positions(ids_to_update_, *transaction, *osm_base_settings().RELATIONS);
        
  // Collect all data of existing and explicitly changed skeletons
  std::map< Uint31_Index, std::set< Relation_Skeleton > > existing_skeletons
      = get_existing_skeletons< Relation_Skeleton >
      (existing_map_positions, *transaction, *osm_base_settings().RELATIONS);
  
  // Collect also all data of existing and implicitly changed skeletons
  std::map< Uint31_Index, std::set< Relation_Skeleton > > implicitly_moved_skeletons
      = get_implicitly_moved_skeletons
          (attic_node_skeletons, attic_way_skeletons,
           existing_skeletons, *transaction, *osm_base_settings().RELATIONS);
  
  // Collect all data of existing meta elements
  std::map< Uint31_Index, std::set< OSM_Element_Metadata_Skeleton< Relation::Id_Type > > > existing_meta
      = (meta ? get_existing_meta< OSM_Element_Metadata_Skeleton< Relation::Id_Type > >
             (existing_map_positions, *transaction, *meta_settings().RELATIONS_META) :
         std::map< Uint31_Index, std::set< OSM_Element_Metadata_Skeleton< Relation::Id_Type > > >());
          
  // Collect all data of existing meta elements
  std::vector< std::pair< Relation_Skeleton::Id_Type, Uint31_Index > > implicitly_moved_positions
      = make_id_idx_directory(implicitly_moved_skeletons);
  std::map< Uint31_Index, std::set< OSM_Element_Metadata_Skeleton< Relation::Id_Type > > > implicitly_moved_meta
      = (meta ? get_existing_meta< OSM_Element_Metadata_Skeleton< Relation::Id_Type > >
             (implicitly_moved_positions, *transaction, *meta_settings().RELATIONS_META) :
         std::map< Uint31_Index, std::set< OSM_Element_Metadata_Skeleton< Relation::Id_Type > > >());

  // Collect all data of existing tags
  std::vector< Tag_Entry< Relation_Skeleton::Id_Type > > existing_local_tags;
  get_existing_tags< Relation_Skeleton::Id_Type >
      (existing_map_positions, *transaction->data_index(osm_base_settings().RELATION_TAGS_LOCAL),
       existing_local_tags);
      
  // Collect all data of existing tags for moved relations
  std::vector< Tag_Entry< Relation_Skeleton::Id_Type > > implicitly_moved_local_tags;
  get_existing_tags< Relation_Skeleton::Id_Type >
      (implicitly_moved_positions, *transaction->data_index(osm_base_settings().RELATION_TAGS_LOCAL),
       implicitly_moved_local_tags);

  // Create a node directory id to idx:
  // Evaluate first the new_node_skeletons
  std::map< Node_Skeleton::Id_Type, Quad_Coord > new_node_idx_by_id
      = dictionary_from_skeletons(new_node_skeletons);
  // Then lookup the missing nodes.
  lookup_missing_nodes(new_node_idx_by_id, new_data, implicitly_moved_skeletons, *transaction);
  
  // Create a node directory id to idx:
  // Evaluate first the new_way_skeletons
  std::map< Way_Skeleton::Id_Type, Uint31_Index > new_way_idx_by_id
      = dictionary_from_skeletons(new_way_skeletons);
  // Then lookup the missing nodes.
  lookup_missing_ways(new_way_idx_by_id, new_data, implicitly_moved_skeletons, *transaction);
  
  // Compute the indices of the new relations
  compute_geometry(new_node_idx_by_id, new_way_idx_by_id, new_data);

  // Compute which objects really have changed
  std::map< Uint31_Index, std::set< Relation_Skeleton > > attic_skeletons;
  std::map< Uint31_Index, std::set< Relation_Skeleton > > new_skeletons;
  new_current_skeletons(new_data, existing_map_positions, existing_skeletons,
      (update_logger != 0), attic_skeletons, new_skeletons, moved_relations, update_logger);
  
  // Compute and add implicitly moved relations
  new_implicit_skeletons(new_node_idx_by_id, new_way_idx_by_id, implicitly_moved_skeletons,
      (update_logger != 0), attic_skeletons, new_skeletons, moved_relations, update_logger);

  // Compute which meta data really has changed
  std::map< Uint31_Index, std::set< OSM_Element_Metadata_Skeleton< Relation_Skeleton::Id_Type > > > attic_meta;
  std::map< Uint31_Index, std::set< OSM_Element_Metadata_Skeleton< Relation_Skeleton::Id_Type > > > new_meta;
  new_current_meta(new_data, existing_map_positions, existing_meta, attic_meta, new_meta);

  // Compute which meta data has moved
  std::vector< std::pair< Relation_Skeleton::Id_Type, Uint31_Index > > new_positions
      = make_id_idx_directory(new_skeletons);
  new_implicit_meta(implicitly_moved_meta, new_positions, attic_meta, new_meta);

  // Compute which tags really have changed
  std::map< Tag_Index_Local, std::set< Relation_Skeleton::Id_Type > > attic_local_tags;
  std::map< Tag_Index_Local, std::set< Relation_Skeleton::Id_Type > > new_local_tags;
  new_current_local_tags< Relation_Skeleton, Update_Relation_Logger, Relation_Skeleton::Id_Type >
      (new_data, existing_map_positions, existing_local_tags, attic_local_tags, new_local_tags);
  new_implicit_local_tags(implicitly_moved_local_tags, new_positions, attic_local_tags, new_local_tags);
  std::map< Tag_Index_Global, std::set< Relation_Skeleton::Id_Type > > attic_global_tags;
  std::map< Tag_Index_Global, std::set< Relation_Skeleton::Id_Type > > new_global_tags;
  new_current_global_tags< Relation_Skeleton::Id_Type >
      (attic_local_tags, new_local_tags, attic_global_tags, new_global_tags);
  
  add_deleted_skeletons(attic_skeletons, new_positions);

  //TODO: old code
  if (update_logger && meta)
  {
    for (vector< pair< OSM_Element_Metadata_Skeleton< Relation::Id_Type >, uint32 > >::const_iterator
        it = rels_meta_to_insert.begin(); it != rels_meta_to_insert.end(); ++it)
    {
      OSM_Element_Metadata meta;
      meta.version = it->first.version;
      meta.timestamp = it->first.timestamp;
      meta.changeset = it->first.changeset;
      meta.user_id = it->first.user_id;
      meta.user_name = user_by_id[it->first.user_id];
      update_logger->insertion(it->first.ref, meta);
    }
  }

  callback->update_started();
  callback->prepare_delete_tags_finished();
  
  // Update id indexes
  update_map_positions(new_positions, *transaction, *osm_base_settings().RELATIONS);
  callback->update_ids_finished();
  
  // Update skeletons
  update_elements(attic_skeletons, new_skeletons, *transaction, *osm_base_settings().RELATIONS, update_logger);
  callback->update_coords_finished();
  
  // Update meta
  if (meta)
    update_elements(attic_meta, new_meta, *transaction, *meta_settings().RELATIONS_META, update_logger);
  
  // Update local tags
  update_elements(attic_local_tags, new_local_tags, *transaction, *osm_base_settings().RELATION_TAGS_LOCAL,
                  update_logger);
  callback->tags_local_finished();
  
  // Update global tags
  update_elements(attic_global_tags, new_global_tags, *transaction, *osm_base_settings().RELATION_TAGS_GLOBAL);
  callback->tags_global_finished();
  
  flush_roles();
  callback->flush_roles_finished();
  
  if (meta != only_data)
  {
    map< uint32, vector< uint32 > > idxs_by_id;
    create_idxs_by_id(rels_meta_to_insert, idxs_by_id);
    process_user_data(*transaction, user_by_id, idxs_by_id);
    
    if (update_logger)
    {
      stable_sort(rels_meta_to_delete.begin(), rels_meta_to_delete.begin());
      rels_meta_to_delete.erase(unique(rels_meta_to_delete.begin(), rels_meta_to_delete.end()),
				 rels_meta_to_delete.end());
      update_logger->set_delete_meta_data(rels_meta_to_delete);
      rels_meta_to_delete.clear();
    }
  }
  callback->update_finished();

  new_data.data.clear();
  rels_meta_to_delete.clear();
  rels_meta_to_insert.clear();
  
  if (!external_transaction)
    delete transaction;
}


vector< Uint31_Index > calc_node_idxs(const vector< uint32 >& node_idxs)
{
  vector< Uint31_Index > result;
  for (vector< uint32 >::size_type i = 0; i < node_idxs.size(); ++i)
    result.push_back((node_idxs[i] & 0x7fffff00) | 0x80000002);
  sort(result.begin(), result.end());
  result.erase(unique(result.begin(), result.end()), result.end());
  
  return result;
}

vector< Uint31_Index > calc_way_idxs(const vector< uint32 >& way_idxs)
{
  vector< Uint31_Index > result;
  for (vector< uint32 >::size_type i = 0; i < way_idxs.size(); ++i)
    result.push_back(way_idxs[i]);
  sort(result.begin(), result.end());
  result.erase(unique(result.begin(), result.end()), result.end());
  
  return result;
}


void Relation_Updater::flush_roles()
{
  map< Uint32_Index, set< String_Object > > db_to_delete;
  map< Uint32_Index, set< String_Object > > db_to_insert;
  
  for (map< string, uint32 >::const_iterator it(role_ids.begin());
      it != role_ids.end(); ++it)
  {
    if (it->second >= max_written_role_id)
      db_to_insert[Uint32_Index(it->second)].insert
      (String_Object(it->first));
    if (it->second >= max_role_id)
      max_role_id = it->second + 1;
  }
  
  Block_Backend< Uint32_Index, String_Object > roles_db
      (transaction->data_index(osm_base_settings().RELATION_ROLES));
  roles_db.update(db_to_delete, db_to_insert);
  max_written_role_id = max_role_id;
}


vector< string > Relation_Updater::get_roles()
{
  if (max_role_id == 0)
    load_roles();
  
  vector< string > roles(max_role_id);
  for (map< string, uint32 >::const_iterator it(role_ids.begin());
      it != role_ids.end(); ++it)
    roles[it->second] = it->first;
  return roles;
}
