#include "gui/main/group_list.h"
#include "shared/tool.h"
#include "gui/main/control/friend_item.h"
#include "gui/main/control/session_item.h"
#include "module/service/team_service.h"
#include "module/session/session_manager.h"

GroupList::GroupList(ui::TreeView* group_list) :
	group_list_(group_list)
{
	auto query_cb = nbase::Bind(&GroupList::QueryAllMyTeamsInfoAsync, this, std::placeholders::_1, std::placeholders::_2);
	nim::DataSync::RegCompleteCb(query_cb);
	nim::Team::QueryAllMyTeamsInfoAsync(nbase::Bind(&GroupList::OnQueryAllMyTeams, this, std::placeholders::_1, std::placeholders::_2));

	auto add_team_cb = nbase::Bind(&GroupList::OnAddTeam, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
	unregister_cb.Add(nim_ui::TeamManager::GetInstance()->RegAddTeam(add_team_cb));
	auto remove_team_cb = nbase::Bind(&GroupList::OnRemoveTeam, this, std::placeholders::_1);
	unregister_cb.Add(nim_ui::TeamManager::GetInstance()->RegRemoveTeam(remove_team_cb));
	auto change_team_name_cb = nbase::Bind(&GroupList::OnTeamNameChanged, this, std::placeholders::_1);
	unregister_cb.Add(nim_ui::TeamManager::GetInstance()->RegChangeTeamName(change_team_name_cb));
	
	//���Ӵ���������Ⱥ��listitem��λ���б��ͷλ��
	ui::TreeNode* create_group_item = (ui::TreeNode*)ui::GlobalManager::CreateBox(L"main/main_create_normal_group_list_item.xml");
	group_list_->GetRootNode()->AddChildNodeAt(create_group_item, 0);

	ui::TreeNode* create_advanced_group_item = (ui::TreeNode*)ui::GlobalManager::CreateBox(L"main/main_create_advanced_group_list_item.xml");
	group_list_->GetRootNode()->AddChildNodeAt(create_advanced_group_item, 1);

	ui::TreeNode* search_group_item = (ui::TreeNode*)ui::GlobalManager::CreateBox(L"main/main_search_group_list_item.xml");
	group_list_->GetRootNode()->AddChildNodeAt(search_group_item, 2);

	//��ʼ��������
	ui::TreeNode* tree_node;
	tree_node = ListItemUtil::CreateFirstLetterListItem(L"�߼�Ⱥ");
	group_list_->GetRootNode()->AddChildNode(tree_node);
	tree_node_ver_.push_back(tree_node);
	tree_node->SetVisible(false);
	tree_node->SetEnabled(false);

	tree_node = ListItemUtil::CreateFirstLetterListItem(L"��ͨȺ");
	group_list_->GetRootNode()->AddChildNode(tree_node);
	tree_node_ver_.push_back(tree_node);
	tree_node->SetVisible(false);
	tree_node->SetEnabled(false);
}

void GroupList::QueryAllMyTeamsInfoAsync(nim::NIMDataSyncType sync_type, nim::NIMDataSyncStatus status)
{
	if (sync_type == nim::kNIMDataSyncTypeTeamInfo)
	{
		nim::Team::QueryAllMyTeamsInfoAsync(nbase::Bind(&GroupList::OnQueryAllMyTeams, this, std::placeholders::_1, std::placeholders::_2));
	}
}

void GroupList::OnQueryAllMyTeams(int team_count, const std::list<nim::TeamInfo>& team_info_list)
{
	for (auto it = team_info_list.begin(); it != team_info_list.end(); it++)
	{
		AddListItem(*it);
	}
}

GroupList::~GroupList()
{

}

void GroupList::AddListItem(const nim::TeamInfo& all_info)
{
	ui::TreeNode* tree_node;
	if (all_info.type == nim::kNIMTeamTypeAdvanced)
	{
		tree_node = tree_node_ver_[0];
	}
	else
	{
		tree_node = tree_node_ver_[1];
	}
	AddListItemInGroup(all_info, tree_node);
}

void GroupList::AddListItemInGroup(const nim::TeamInfo& all_info, ui::TreeNode* tree_node)
{
	if (tree_node->GetChildNodeCount() == 0)
	{
		tree_node->SetVisible(true);
	}

	FriendItem* item = new FriendItem;
	ui::GlobalManager::FillBoxWithCache( item, L"main/friend_item.xml" );
	UserInfo user_info;
	user_info.account = all_info.id;
	user_info.name = all_info.name;
	item->Init(true, user_info);
	FriendItem* container_element = item;
	std::size_t index = 0;
	for (index = 0; index < tree_node->GetChildNodeCount(); index++)
	{
		FriendItem* temp = (FriendItem*)tree_node->GetChildNode(index);
		if (*container_element == *temp)
		{
			//delete item;
			//ASSERT(FALSE);
			return;
		}
		if (*container_element < *temp)
		{
			tree_node->AddChildNodeAt(container_element, index);
			break;
		}
	}

	if (index == tree_node->GetChildNodeCount())
	{
		tree_node->AddChildNodeAt(container_element, index);
	}
	nim_ui::SessionManager::GetInstance()->QueryMyTList(user_info.account);
}

void GroupList::OnAddTeam(const std::string& tid, const std::string& tname, nim::NIMTeamType type)
{
	OnRemoveTeam(tid);
	nim::TeamInfo user_info;
	user_info.id = tid;
	user_info.name = tname;
	user_info.type = type;
	AddListItem(user_info);
}

void GroupList::OnTeamNameChanged(const nim::TeamInfo& team_info)
{
	for (int i = 0; i < group_list_->GetCount(); i++)
	{
		FriendItem* friend_item = dynamic_cast<FriendItem*>(group_list_->GetItemAt(i));
		if (friend_item)
		{
			if (friend_item->GetId() == team_info.id)
			{
				OnAddTeam(team_info.id, team_info.name, team_info.type);
				break;
			}
		}
	}
}

void GroupList::OnRemoveTeam(const std::string& tid)
{
	for (int i = 0; i < group_list_->GetCount(); i++)
	{
		FriendItem* friend_item = dynamic_cast<FriendItem*>(group_list_->GetItemAt(i));
		if (friend_item)
		{
			if (friend_item->GetId() == tid)
			{
				friend_item->GetParentNode()->RemoveChildNode(friend_item);
				break;
			}
		}
	}
}
