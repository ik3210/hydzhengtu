/**
* \brief 国家管理器
*
* 
*/
#include <zebra/SessionServer.h>
#include <math.h>

CCountryM::CCountryM()
{
	isBeging = false;
	clearForbidTime = 0;
}

/**
* \brief 国家管理器初始化
* \return true 初始化成功 false初始化失败
*/
bool CCountryM::init()
{
	return this->load();
}

/**
* \brief 析构管理器
*/
void CCountryM::destroyMe()
{
	for (DWORD i=0; i<CCountryM::getMe().countries.size(); i++)
	{
		CCountryM::getMe().countries[i]->writeDatabase();
	}
}

/**
* \brief 析构管理器
*/
void CCountryM::save()
{
	this->destroyMe();
}


/**
* \brief 从数据库中加载国家记录
* \return true 加载成功
*/
bool CCountryM::load()
{
	DBFieldSet* country = SessionService::metaData->getFields("COUNTRY");

	if (country)
	{
		connHandleID handle = SessionService::dbConnPool->getHandle();

		if ((connHandleID)-1 == handle)
		{
			Zebra::logger->error("不能获取数据库句柄");
			return false;
		}

		DBRecordSet* recordset = NULL;

		if ((connHandleID)-1 != handle)
		{
			recordset = SessionService::dbConnPool->exeSelect(handle,country,NULL,NULL);
		}

		SessionService::dbConnPool->putHandle(handle);

		if (recordset)
		{
			if( recordset->size() == 0 )
			{
				Zebra::logger->error("国家数据加载失败,记录数为0.");
			}
			for (DWORD i=0; i<recordset->size(); i++)
			{
				DBRecord* rec = recordset->get(i);
				CCountry* pCountry = new CCountry();

				if (pCountry)
				{
					pCountry->init(rec);
					pCountry->loadTechFromDB();

					if (CUnionM::getMe().getUnionByID(pCountry->dwKingUnionID) == NULL)
					{//如果拥有者
						pCountry->dwKingUnionID = 0;
						bzero(pCountry->kingName,sizeof(pCountry->kingName));
						Zebra::logger->info("帮会已不存在,%s:国王清空",pCountry->name);
					}
				}

				countries.push_back(pCountry);
			}

			SAFE_DELETE(recordset)
		}
	}
	else
	{
		Zebra::logger->error("国家数据加载失败,COUNTRY表不存在");
		return false;
	}

	return true;
}

CCountry* CCountryM::addNewCountry(DWORD country)
{
	CCountry* pCountry = new CCountry();
	rwlock.wrlock();

	if (pCountry)
	{
		pCountry->dwID = country;

		if (pCountry->insertDatabase())
		{
			countries.push_back(pCountry);
		}
		else
		{
			SAFE_DELETE(pCountry);
		}
	}

	rwlock.unlock();

	return pCountry;
}


CCountry* CCountryM::find(DWORD country,DWORD unionid)
{
	CCountry* pCountry = NULL;

	rwlock.rdlock();
	for (DWORD i=0; i<countries.size(); i++)
	{
		if (countries[i]->dwID==country && countries[i]->dwKingUnionID == unionid)
		{
			pCountry = countries[i];
			break;
		}
	}
	rwlock.unlock();

	return pCountry;
}

CCountry* CCountryM::find(DWORD country)
{
	CCountry* pCountry = NULL;

	rwlock.rdlock();
	for (DWORD i=0; i<countries.size(); i++)
	{
		if (countries[i]->dwID==country)
		{
			pCountry = countries[i];
			break;
		}
	}

	rwlock.unlock();

	return pCountry;
}

CCountry* CCountryM::findByDare(DWORD country,bool findDare)
{
	CCountry* pCountry = NULL;
	bool isDare = false;  
	DWORD dwDefCountryID = 0;

	rwlock.rdlock();
	for (DWORD i=0; i<countries.size(); i++)
	{
		if (countries[i]->dwDareCountryID == country)
		{
			isDare = true;
			dwDefCountryID = countries[i]->dwID;
			break;
		}
	}
	rwlock.unlock();
	if (findDare)
	{
		if (isDare)
		{
			pCountry = this->find(country);
		}
	}
	else
	{
		if (isDare)
		{
			pCountry = this->find(dwDefCountryID);
		}
	}

	return pCountry;
}

void CCountryM::timerPerHour()
{
	struct tm tv1;
	time_t timValue = time(NULL);
	zRTime::getLocalTime(tv1,timValue);

	std::vector<CCountry*>::iterator vIterator;
	rwlock.rdlock();

	for(vIterator = countries.begin(); vIterator!=countries.end(); vIterator++)
	{
		if (tv1.tm_wday == 1 && CVoteM::getMe().find((*vIterator)->dwID,Cmd::TECH_VOTE)==NULL)
		{
			(*vIterator)->beginTechVote();
		}
	}

	rwlock.unlock();
}

void CCountry::setWinnerExp()
{
	winner_exp = true;
	winner_time = 1;
	Cmd::Session::t_updateWinnerExp_SceneSession send;
	send.countryID = dwID;
	send.type = winner_exp;
	SessionTaskManager::getInstance().broadcastScene(&send,sizeof(send));
}
void CCountry::checkWinnerExp()
{
	if (winner_time)
		winner_time++;
	if (winner_time >= 60 * 3)
	{
		winner_time= 0;
		winner_exp = false;
		Cmd::Session::t_updateWinnerExp_SceneSession send;
		send.countryID = dwID;
		send.type = winner_exp;
		SessionTaskManager::getInstance().broadcastScene(&send,sizeof(send));
	}
}
void CCountryM::timer()
{
	//检测战胜国经验加成标志
	std::vector<CCountry*>::iterator iter;
	rwlock.rdlock();
	for(iter = countries.begin(); iter!=countries.end(); iter++)
	{
		(*iter)->checkWinnerExp();
		if ((*iter)->gen_refreshTime && (*iter)->gen_refreshTime<=SessionTimeTick::currentTime.sec())
			refreshGeneral((*iter)->dwID);
	}  
	rwlock.unlock();


	struct tm tv1;
	time_t timValue = time(NULL);
	zRTime::getLocalTime(tv1,timValue);
	static time_t lastMessage = 0;

	if (tv1.tm_hour==20 && tv1.tm_min>=10)
	{
		if (tv1.tm_min<35 && timValue-lastMessage > 10*60)
		{
			std::vector<CCountry*>::iterator vIterator;
			rwlock.rdlock();

			for(vIterator = countries.begin(); vIterator!=countries.end(); vIterator++)
			{
				if ((*vIterator)->dwDareCountryID>0 && (timValue-(*vIterator)->dwLastDareTime > 24*60*60))
				{
					CCountry* pCountry = this->find((*vIterator)->dwDareCountryID);
					if (pCountry)
					{
						SessionChannel::sendCountryInfo(Cmd::INFO_TYPE_GAME,(*vIterator)->dwID,
							"还剩 %d 分钟 %s　国将来攻打我国王城",
							::abs(40-tv1.tm_min),pCountry->name);

						SessionChannel::sendCountryInfo(Cmd::INFO_TYPE_GAME,(*vIterator)->dwDareCountryID,
							"还剩 %d 分钟 我国将去攻打 %s 王城",
							::abs(40-tv1.tm_min),(*vIterator)->name);
					}
				}
			}  
			rwlock.unlock();

			lastMessage = timValue;
		}

		if (tv1.tm_min>=35 && tv1.tm_min<40 && timValue-lastMessage > 60)
		{
			std::vector<CCountry*>::iterator vIterator;
			rwlock.rdlock();

			for(vIterator = countries.begin(); vIterator!=countries.end(); vIterator++)
			{

				if ((*vIterator)->dwDareCountryID>0
					&& (timValue-(*vIterator)->dwLastDareTime > 24*60*60))
				{

					CCountry* pCountry = this->find((*vIterator)->dwDareCountryID);
					if (pCountry)
					{
						SessionChannel::sendCountryInfo(Cmd::INFO_TYPE_GAME,(*vIterator)->dwID,
							"还剩 %d 分钟 %s　国将来攻打我国王城",
							::abs(40-tv1.tm_min),pCountry->name);

						SessionChannel::sendCountryInfo(Cmd::INFO_TYPE_GAME,(*vIterator)->dwDareCountryID,
							"还剩 %d 分钟 我国将去攻打 %s 王城",
							::abs(40-tv1.tm_min),(*vIterator)->name);
					}
				}
			}  
			rwlock.unlock();

			lastMessage = timValue;
		}
	}

	if (tv1.tm_hour==20 && (tv1.tm_min>=40 && tv1.tm_min<43) && !isBeging)
	{// 到八点开始新建正式国战对战对象
		this->beginDare();
		isBeging = true; // 在晚上十点强制结束还存在的对战时,重置为false
	}

	if (tv1.tm_hour>=20 && tv1.tm_hour<=22)
	{
		if (tv1.tm_min%10 == 0 && timValue-lastMessage>60)
		{
			std::vector<CCountry*>::iterator vIterator;

			for(vIterator = countries.begin(); vIterator!=countries.end(); vIterator++)
			{
				if ((*vIterator)->dwDareCountryID>0
					&& (timValue-(*vIterator)->dwLastDareTime > 24*60*60))
				{
					CCountry* pCountry = this->find((*vIterator)->dwDareCountryID);
					if (pCountry)
					{
						SessionChannel::sendCountryInfo(Cmd::INFO_TYPE_GAME,(*vIterator)->dwID,
							"%s 正在围攻我国的王城,请回城防御或反攻解围",
							pCountry->name);

						SessionChannel::sendCountryInfo(Cmd::INFO_TYPE_GAME,
							(*vIterator)->dwDareCountryID,
							"我国攻打 %s 的战斗正在进行,各位爱国志士可以通过边防官到 %s 参加战斗",
							(*vIterator)->name,(*vIterator)->name);
					}
				}
			}  
			lastMessage = timValue;
		}
	}

	if (tv1.tm_hour==22 && tv1.tm_min>=40 && tv1.tm_min<44)
	{
		isBeging = false;
		this->endDare();
	}

	if (tv1.tm_hour==0 && tv1.tm_min<5 && SessionTimeTick::currentTime.sec()>clearForbidTime)
	{
		clearForbid();
		clearForbidTime = SessionTimeTick::currentTime.sec();
		clearForbidTime += 86000;
		this->resetCallTimes();
		this->clearDiplomat();
		this->clearCatcher();
	}
}

bool CCountryM::processUserMessage(UserSession *pUser,const Cmd::stNullUserCmd *pNullCmd,const DWORD cmdLen)
{
	switch (pNullCmd->byParam) 
	{
	case REQ_DAILY_EMPEROR_MONEY:
		{
			Cmd::stReqDailyEmperorMoneyCmd* rev = (Cmd::stReqDailyEmperorMoneyCmd*)pNullCmd;
			this->processReqDailyEmperorMoney(pUser,rev);
			return true;
		}
		break;
	case CANCEL_DIPLOMAT_PARA:
		{
			Cmd::stCancelDiplomatCmd* rev = (Cmd::stCancelDiplomatCmd*)pNullCmd;
			processCancelDiplomat(pUser,rev);
			return true;
		}
		break;
	case CANCEL_CATCHER_PARA:
		{
			Cmd::stCancelCatcherCmd* rev = (Cmd::stCancelCatcherCmd*)pNullCmd;
			processCancelCatcher(pUser,rev);
			return true;
		}
		break;
	case APPOINT_CATCHER_PARA:
		{
			Cmd::stAppointCatcherCmd* rev = (Cmd::stAppointCatcherCmd*)pNullCmd;
			processSetCatcher(pUser,rev);

			return true;
		}
		break;
	case APPOINT_DIPLOMAT_PARA:
		{
			Cmd::stAppointDiplomatCmd* rev = (Cmd::stAppointDiplomatCmd*)pNullCmd;
			processSetDiplomat(pUser,rev);
			return true;
		}
		break;
	case REQ_COUNTRY_NOTE_PARA:
		{
			CCountry* pCountry = this->find(pUser->country);
			if (pCountry && strncmp(pCountry->note,"",255))
			{
				Cmd::stCountryNoteCmd send;
				strncpy(send.note,pCountry->note,255);
				pUser->sendCmdToMe(&send,sizeof(send));
			}
			return true;
		}
		break;
	case COUNTRY_NOTE_PARA:
		{
			Cmd::stCountryNoteCmd * rev = (Cmd::stCountryNoteCmd *)pNullCmd;
			CCountry* pCountry = this->find(pUser->country);
			if (pCountry && pCountry->isKing(pUser))
			{
				strncpy(pCountry->note,rev->note,255);

				SessionChannel::sendCountry(pUser->country,rev,sizeof(Cmd::stCountryNoteCmd));
			}
			return true;
		}
		break;
	case ANTI_DARE_COUNTRY_FORMAL_PARA:
		{
			Cmd::stAntiDareCountryFormalCmd* rev = (Cmd::stAntiDareCountryFormalCmd*)pNullCmd;
			processAntiDareCountry(pUser,rev);
			return true;
		}
		break;
	case CANCEL_TECH_SEARCH_PARA:
		{
			Cmd::stCancelTechSearchUserCmd* rev = (Cmd::stCancelTechSearchUserCmd*)pNullCmd;
			processCancelTechSearch(pUser,rev);
			return true;
		}
		break;
	case REQ_WAIT_OFFICIAL_PARA:
		{
			Cmd::stReqWaitOfficialUserCmd* rev = (Cmd::stReqWaitOfficialUserCmd*)pNullCmd;
			processReqWaitOfficial(pUser,rev);
			return true;
		}
		break;
	case CONFIRM_SEARCHER_PARA:
		{
			Cmd::stConfirmSearcherUserCmd* rev = (Cmd::stConfirmSearcherUserCmd*)pNullCmd;
			processConfirmSearcher(pUser,rev);
			return true;
		}
		break;
	case SET_TECH_SEARCH_PARA:
		{
			Cmd::stSetTechSearchUserCmd* rev = (Cmd::stSetTechSearchUserCmd*)pNullCmd;
			processSetTechSearch(pUser,rev);
			return true;
		}
		break;
	case UP_TECH_DEGREE_PARA:
		{
			Cmd::stUpTechDegreeUserCmd* rev = (Cmd::stUpTechDegreeUserCmd*)pNullCmd;
			processUpTech(pUser,rev);
			return true;
		}
		break;
	case  REQ_TECH_PARA:
		{
			Cmd::stReqTechUserCmd *rev = (Cmd::stReqTechUserCmd *)pNullCmd;
			processRequestTech(pUser,rev);
			return true;
		}
		break;
	case Cmd::SELECT_TRANS_LEVEL:
		{
			Cmd::stSelectTransLevelUserCmd* rev = (Cmd::stSelectTransLevelUserCmd*)pNullCmd;

			// TODO,检查是否是国王,并检查是否在国战期
			CUnion* pUnion = CUnionM::getMe().getUnionByID(pUser->unionid);

			if (pUnion)
			{
				if (pUnion->master && (pUnion->master->id == pUser->id))
				{
					CCity* pCity = CCityM::getMe().find(pUser->country,KING_CITY_ID); // 王城
					CCountry* pCountry = this->find(pUser->country);

					if (pCountry && pCity)
					{
						if (pCity->dwUnionID == pUser->unionid)
						{//是国王
							CDare* pDare = CDareM::getMe().findDareRecordByID(
								Cmd::COUNTRY_FORMAL_DARE,pUser->country);

							if (pDare)
							{
								Cmd::Session::t_selTransCountryWar_SceneSession send;
								send.dwCountryID = pUser->country;
								send.dwLevel = rev->dwLevel;
								SessionTaskManager::getInstance().broadcastScene(&send,sizeof(send));
								pUser->sendSysChat(Cmd::INFO_TYPE_MSG,
									"您已经成功点燃烽火台。此刻您的国家的各路英雄好汉正在边境处等待着您的一声令下。");

							}
							else
							{
								pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,
									"您不在国战状态,不能点燃烽火台");
							}
						}
						else
						{
							pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,
								"您不是国王,不能点燃烽火台");
						}
					}
					else
					{
						pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,
							"您不是国王,不能点燃烽火台");
					}
				}
				else
				{
					pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"您不是国王,不能点燃烽火台");
				}
			}
			else
			{
				pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"您不是国王,不能点燃烽火台");
			}
			return true;
		}
		break;
	case KING_PUNISH_COUNTRY_PARA:
		{
			if (!pUser) return false;

			CCountry* pCountry = find(pUser->country);
			if (!pCountry) return false;

			if (strcmp(pUser->name,pCountry->kingName)) return true;

			if (pCountry->sendPrison!=0)
			{
				pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"你今天已经关押过一个玩家");
				return true;
			}

			Cmd::stKingPunishCountryCmd * rev = (Cmd::stKingPunishCountryCmd *)pNullCmd;
			if (0==strncmp(rev->name,pUser->name,MAX_NAMESIZE))
			{
				pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"不能关押自己");
				return true;
			}

			UserSession *u = UserSessionManager::getInstance()->getUserSessionByName(rev->name);
			if (!u)
			{
				pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"玩家 %s 不在线",rev->name);
				return true;
			}

			if (pUser->country!=u->country)
			{
				pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"你只能关押自己的国民");
				return true;
			}

			Cmd::Session::t_countryPunishUser_SceneSession send;
			strncpy(send.name,rev->name,MAX_NAMESIZE);
			send.method = 2;
			u->scene->sendCmd(&send,sizeof(send));

			Zebra::logger->info("用户%s(%ld)被国王关进监狱",u->name,u->id);
			SessionChannel::sendCountryInfo(Cmd::INFO_TYPE_GAME,pUser->country,"%s 被国王 %s 关押一小时",u->name,pUser->name);
			pCountry->sendPrison = 1;

			return true;
		}
		break;
	case FORBID_TALK_COUNTRY_PARA:
		{
			if (!pUser) return false;

			CCountry* pCountry = find(pUser->country);
			if (!pCountry) return false;

			if (strcmp(pUser->name,pCountry->kingName)) return true;

			if (pCountry->forbidTalk!=0)
			{
				pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"你今天已经禁言过一个玩家");
				return true;
			}

			Cmd::stForbidTalkCountryUserCmd * rev = (Cmd::stForbidTalkCountryUserCmd *)pNullCmd;
			if (0==strncmp(rev->name,pUser->name,MAX_NAMESIZE))
			{
				pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"不能禁言自己");
				return true;
			}

			UserSession *u = UserSessionManager::getInstance()->getUserSessionByName(rev->name);
			if (!u)
			{
				pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"玩家 %s 不在线",rev->name);
				return true;
			}

			if (pUser->country!=u->country)
			{
				pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"你只能禁言自己的国民");
				return true;
			}

			Cmd::Session::t_countryPunishUser_SceneSession send;
			strncpy(send.name,rev->name,MAX_NAMESIZE);
			send.method = 1;
			u->scene->sendCmd(&send,sizeof(send));

			SessionChannel::sendCountryInfo(Cmd::INFO_TYPE_GAME,pUser->country,"%s 被国王 %s 禁言一小时",u->name,pUser->name);
			pCountry->forbidTalk = 1;

			return true;
		}
		break;
	case EMPEROR_PUNISH_COUNTRY_PARA:
		{
			if (!pUser) return false;

			if (!CCountryM::getMe().isEmperor(pUser)) return true;

			if (EmperorForbid::getMe().count()>=10)
			{
				pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"你今天已经禁言过 10 个玩家");
				return true;
			}

			Cmd::stEmperorPunishCountryCmd * rev = (Cmd::stEmperorPunishCountryCmd *)pNullCmd;
			if (0==strncmp(rev->name,pUser->name,MAX_NAMESIZE))
			{
				pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"不能处罚自己");
				return true;
			}

			UserSession *u = UserSessionManager::getInstance()->getUserSessionByName(rev->name);
			if (!u)
			{
				pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"玩家 %s 不在线",rev->name);
				return true;
			}

			if (1==rev->method)//禁言
			{
				if (!EmperorForbid::getMe().add(u->id))
				{
					pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"你今天已经禁言 %s 一次了",rev->name);
					return true;
				}

				Cmd::Session::t_countryPunishUser_SceneSession send;
				strncpy(send.name,rev->name,MAX_NAMESIZE);
				send.method = 1;
				u->scene->sendCmd(&send,sizeof(send));

				SessionChannel::sendAllInfo(Cmd::INFO_TYPE_GAME,"%s 被皇帝 %s 禁言一小时",u->name,pUser->name);
			}
			return true;
		}
		break;
	case Cmd::TRANS_DARE_COUNTRY_PARA:
		{
			//TODO：取出要去的战场国家,判断要去的国家ID和挑战的ID是否有与该玩家相同的。有则是去参战国,否则就要收取一两银子。向场景发送转送命令
			/*Cmd::stTransDareCountryCmd* rev = (Cmd::stTransDareCountryCmd*)pNullCmd;
			CCountry* pCountry = CCountryM::getMe().find(rev->dwCountryID);  
			Cmd::Session::t_transDareCountry_SceneSession  send;
			send.dwUserID = pUser->id;

			if (pCountry->dwDareCountryID>0 && isBeging)
			{
			if (pCountry->dwID==pUser->country || pCountry->dwDareCountryID==pUser->country)
			{
			send.dwMoney = 0;
			}
			else
			{
			send.dwMoney = 100; // 1两
			}

			send.dwCountry = rev->dwCountryID;
			if (pUser->scene) pUser->scene->sendCmd(&send,sizeof(send));
			}
			else
			{
			pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"您不在国战状态,不允许使用该功能。");
			}*/

			return true;
		}
		break;
	case Cmd::REQUEST_DARE_COUNTRY_PARA:
		{
			Cmd::stRequestDareCountryCmd *rev = (Cmd::stRequestDareCountryCmd *)pNullCmd;
			processRequestDare(pUser,rev);
			return true;
		}
		break;
	case Cmd::DARE_COUNTRY_FORMAL_PARA:
		{
			Cmd::stDareCountryFormalCmd* rev = (Cmd::stDareCountryFormalCmd*)pNullCmd;
			processDareCountry(pUser,rev);
			return true;
		}
		break;
	case Cmd::REQUEST_DARE_RECORD_PARA:
		{
			Cmd::stRequestDareRecordCmd* rev = (Cmd::stRequestDareRecordCmd*)pNullCmd;
			if (rev->byType == Cmd::DARE_RECORD_RESULT)
			{
				return CDareRecordM::getMe().processUserMessage(pUser,pNullCmd,cmdLen);
			}
			else if (rev->byType == Cmd::DARE_RECORD_STAT)
			{
				BYTE buf[zSocket::MAX_DATASIZE];
				Cmd::stDareStat *tempStat = NULL;

				std::vector<CCountry*>::iterator vIterator;

				rwlock.rdlock();
				Cmd::stReturnDareRecordStatCmd *retCmd=(Cmd::stReturnDareRecordStatCmd *)buf;

				constructInPlace(retCmd);
				tempStat = (Cmd::stDareStat *)retCmd->dare_stat;
				retCmd->dwSize = 0;

				for (vIterator = countries.begin(); vIterator!=countries.end(); vIterator++)
				{
					tempStat->dwCountry = (*vIterator)->dwID;
					tempStat->dwFormalWin = (*vIterator)->dwFormalWin;
					tempStat->dwFormalFail = (*vIterator)->dwFormalFail;
					tempStat->dwAnnoyWin = (*vIterator)->dwAnnoyWin;
					tempStat->dwAnnoyFail = (*vIterator)->dwAnnoyFail;

					tempStat++;
					retCmd->dwSize++;
				}
				rwlock.unlock();

				pUser->sendCmdToMe(retCmd,
					(retCmd->dwSize*sizeof(Cmd::stDareStat)+sizeof(Cmd::stReturnDareRecordStatCmd)));

			}
			else if (rev->byType == Cmd::DARE_RECORD_PLAN)
			{
				BYTE buf[zSocket::MAX_DATASIZE];
				Cmd::stDarePlan *tempPlan = NULL;

				std::vector<CCountry*>::iterator vIterator;

				rwlock.rdlock();
				Cmd::stReturnDareRecordPlanCmd  *retCmd=(Cmd::stReturnDareRecordPlanCmd *)buf;

				constructInPlace(retCmd);
				tempPlan = (Cmd::stDarePlan *)retCmd->dare_plan;
				retCmd->dwSize = 0;

				for (vIterator = countries.begin(); vIterator!=countries.end(); vIterator++)
				{
					if ((*vIterator)->dwDareCountryID > 0)
					{
						tempPlan->attCountry = (*vIterator)->dwDareCountryID;
						tempPlan->defCountry = (*vIterator)->dwID;
						tempPlan->planTime = (*vIterator)->dwLastDareTime;
						tempPlan++;
						retCmd->dwSize++;
					}
				}

				rwlock.unlock();

				pUser->sendCmdToMe(retCmd,
					(retCmd->dwSize*sizeof(Cmd::stDarePlan)+sizeof(Cmd::stReturnDareRecordPlanCmd)));

			}

			return true;
		}
		break;
	case Cmd::TAX_COUNTRY_PARA:
		{
			Cmd::stTaxCountryUserCmd* rev = (Cmd::stTaxCountryUserCmd*)pNullCmd;
			CCountry * pCountry = this->find(rev->dwCountry);
			if (pCountry)
			{
				Cmd::stTaxCountryUserCmd send;
				send.byTax = (BYTE)pCountry->dwTax;
				send.dwCountry = pCountry->dwID;
				pUser->sendCmdToMe(&send,sizeof(send));
			}
			return true;
		}
		break;
	case Cmd::FISK_COUNTRY_PARA:
		{
			Cmd::stFiskCountryUserCmd* rev = (Cmd::stFiskCountryUserCmd*)pNullCmd;
			CCountry * pCountry = this->find(rev->dwCountry);
			if (pCountry)
			{
				Cmd::stFiskCountryUserCmd send;
				send.qwGold = pCountry->qwGold;
				send.qwStock = pCountry->qwStock;
				send.qwMaterial = pCountry->qwMaterial;
				send.dwCountry = pCountry->dwID;

				pUser->sendCmdToMe(&send,sizeof(send));
			}
			return true;
		}
		break;
	case Cmd::SETTAX_COUNTRY_PARA:
		{
			Cmd::stSetTaxCountryUserCmd* rev = (Cmd::stSetTaxCountryUserCmd*)pNullCmd;
			CCountry * pCountry = this->find(rev->dwCountry);
			if (pCountry)
			{
				CCity *pCity = CCityM::getMe().find(pUser->country,KING_CITY_ID);
				if (pCity != NULL && pCity->dwUnionID!=0)
				{
					CUnion *pUnion = CUnionM::getMe().getUnionByID(pCity->dwUnionID);
					if (pUnion)
					{
						if (pUnion->master)
						{
							if (pUnion->master->id == pUser->id) //终于确定他是国王
							{
								if (rev->byTax>=20)
								{
									pCountry->dwTax = 20;
								}
								else
								{
									pCountry->dwTax = rev->byTax;
								}
								pUser->sendSysChat(Cmd::INFO_TYPE_MSG,"税率已经调整成%d\%",pCountry->dwTax);
								SceneSessionManager::getInstance()->notifyCountryTax(pCountry->dwID,pCountry->dwTax);

								Cmd::stTaxCountryUserCmd send;
								send.byTax = (BYTE)pCountry->dwTax;
								send.dwCountry = pCountry->dwID;
								SessionTaskManager::getInstance().sendCmdToWorld(&send,sizeof(send));
								return true;
							}
						}
					}
				}
			}
			pUser->sendSysChat(Cmd::INFO_TYPE_MSG,"税率设置失败!");
			return true;
		}
		break;
	case REQ_GEN_COUNTRY_PARA:
		{
			if (!pUser) return true;
			CCountry * pCountry = find(pUser->country);
			if (!pCountry) return false;

			Cmd::stRetGenCountryCmd send;
			send.level = pCountry->gen_level;
			send.exp = pCountry->gen_exp;
			send.maxExp = pCountry->gen_maxexp;

			pUser->sendCmdToMe(&send,sizeof(send));
			return true;
		}
		break;
	case REQ_KING_LIST_PARA:
		{
			using namespace Cmd;
			char buffer[zSocket::MAX_USERDATASIZE];
			stRtnKingListCmd *send = (stRtnKingListCmd*)buffer;
			constructInPlace(send);
			std::vector<CCountry*>::iterator vIterator;
			for (vIterator = countries.begin(); vIterator!=countries.end(); vIterator++)
			{
				send->data[send->size].country_id = (*vIterator)->dwID;
				if ((*vIterator)->kingtime)
				{
					send->data[send->size].online_time=(SessionTimeTick::currentTime.sec()-(*vIterator)->kingtime)/3600;
				}
				else
				{
					send->data[send->size].online_time=0;
				}

				strncpy(send->data[send->size].king_name,(*vIterator)->name,MAX_NAMESIZE);
				send->size++;
			}
			pUser->sendCmdToMe(send,sizeof(stRtnKingListCmd) + send->size * sizeof(_KingListItem));
			return true;
		}
		break;
	case REQ_CITY_OWNER_LIST_PARA:
		{
			using namespace Cmd;
			struct CityOwnerCallback : public CCityM::cityCallback
			{
				stRtnCityOwnerListCmd *_send;
				CityOwnerCallback(stRtnCityOwnerListCmd *send):_send(send)
				{
				}
				void exec(CCity * pCity)
				{
					if (pCity->getName()!=NULL)
					{
						CUnion *pUnion = CUnionM::getMe().getUnionByID(pCity->dwUnionID);
						if (pUnion)
						{
							strncpy(_send->list[_send->size].cityName,pCity->getName(),MAX_NAMESIZE);
							strncpy(_send->list[_send->size].unionName,pUnion->name,MAX_NAMESIZE);
							_send->size++;
						}
					}
				}
			};
			char buffer[zSocket::MAX_USERDATASIZE];
			stRtnCityOwnerListCmd * send = (stRtnCityOwnerListCmd*)buffer;
			constructInPlace(send);
			CityOwnerCallback oc(send);
			CCityM::getMe().execEveryCity(oc);
			if (send->size)
			{
				pUser->sendCmdToMe(send,sizeof(stRtnCityOwnerListCmd) + send->size * sizeof(send->list[0]));
			}
			return true;
		}
		break;
	case REQ_NPC_OWNER_LIST_PARA:
		{
			using namespace Cmd;
			struct NpcOwnerCallback : public execEntry<CNpcDareObj>
			{
				stRtnNpcOwnerListCmd *_send;
				DWORD _country;
				NpcOwnerCallback(stRtnNpcOwnerListCmd *send,DWORD country):_send(send),_country(country)
				{
				}
				bool exec(CNpcDareObj * pNpc)
				{
					CSept * pSept = CSeptM::getMe().getSeptByID(pNpc->get_holdseptid());
					if (pSept)
					{
						SceneSession * scene = SceneSessionManager::getInstance()->getSceneByID((pNpc->get_country()<<16)+pNpc->get_mapid());
						if (scene)
						{
							strncpy(_send->list[_send->size].septName,pSept->name,MAX_NAMESIZE);
							strncpy(_send->list[_send->size].mapName,scene->name,MAX_NAMESIZE);
							_send->list[_send->size].x=pNpc->get_posx();
							_send->list[_send->size].y=pNpc->get_posy();
							_send->list[_send->size].npcID=pNpc->get_npcid();
							_send->size++;
						}
					}
					return true;
				}
			};

			char buffer[zSocket::MAX_USERDATASIZE];
			stRtnNpcOwnerListCmd * send = (stRtnNpcOwnerListCmd*)buffer;
			constructInPlace(send);
			NpcOwnerCallback nb(send,pUser->country);
			CNpcDareM::getMe().execEveryOne(nb);
			if (send->size)
			{
				pUser->sendCmdToMe(send,sizeof(stRtnNpcOwnerListCmd) + send->size * sizeof(send->list[0]));
			}
			return true;
		}
		break;
	default:
		break;
	}
	return false;
}

bool CCountryM::processSceneMessage(const Cmd::t_NullCmd *pNullCmd,const DWORD cmdLen)
{
	switch (pNullCmd->para) 
	{
	case Cmd::Session::PARA_CHECK_USER:
		{
			Cmd::Session::t_checkUser_SceneSession* rev = 
				(Cmd::Session::t_checkUser_SceneSession*)pNullCmd;

			UserSession* pUser  = UserSessionManager::getInstance()->getUserByID(rev->dwCheckedID);

			if (pUser)
			{
				CCountry* pCountry = this->find(pUser->country);
				if (pCountry)
				{
					pCountry->changeCatcher(pUser);

					UserSession* u  = UserSessionManager::getInstance()->getUserByID(rev->dwCheckID);
					if (u)
					{
						u->sendSysChat(Cmd::INFO_TYPE_MSG,"成功任命 %s 为捕头",pUser->name);
					}
				}
			}

			return true;
		}
		break;
	case Cmd::Session::PARA_ALLY_NPC_CLEAR:
		{
			Cmd::Session::t_allyNpcClear_SceneSession* rev =
				(Cmd::Session::t_allyNpcClear_SceneSession*)pNullCmd;

			CAlly* pAlly = CAllyM::getMe().findAlly(rev->dwCountryID);
			CCountry* pCountry = this->find(rev->dwCountryID);

			if (pAlly)
			{
				pAlly->changeFriendDegree(50);
				CCountry* pAllyCountry = NULL;

				if (pAlly->dwCountryID == rev->dwCountryID)
				{
					pAllyCountry = this->find(pAlly->dwAllyCountryID);
				}
				else
				{
					pAllyCountry = this->find(pAlly->dwCountryID);
				}

				if (pAllyCountry) {
					pAllyCountry->changeMaterial(COUNTRY_MONEY,COUNTRY_ALLY_NPC_HORTATION_MONEY);
					pAllyCountry->changeMaterial(COUNTRY_MATERIAL,COUNTRY_ALLY_NPC_HORTATION_MATERIAL);
				}
			}

			if (pCountry)
			{
				pCountry->changeMaterial(COUNTRY_MONEY,COUNTRY_ALLY_NPC_HORTATION_MONEY);
				pCountry->changeMaterial(COUNTRY_MATERIAL,COUNTRY_ALLY_NPC_HORTATION_MATERIAL);
			}

			return true;
		}
		break;
	case Cmd::Session::PARA_OP_ALLY_FRIENDDEGREE:
		{
			Cmd::Session::t_opAllyFrienddegree_SceneSession* rev = 
				(Cmd::Session::t_opAllyFrienddegree_SceneSession*)pNullCmd;

			CAlly* pAlly = CAllyM::getMe().findAlly(rev->dwCountryID);
			if (pAlly)
			{
				pAlly->changeFriendDegree(rev->friendDegree);
				if (pAlly->friendDegree() == 0)
				{
					CAllyM::getMe().fireAlly(pAlly->dwCountryID,pAlly->dwAllyCountryID);
				}
			}
		}
		break;
	case Cmd::Session::PARA_OP_TECH_VOTE:
		{
			Cmd::Session::t_opTechVote_SceneSession* rev = 
				(Cmd::Session::t_opTechVote_SceneSession*)pNullCmd;

			if (rev->byOpType == 1)
			{
				CCountry* pCountry = this->find(rev->dwCountryID);

				if (pCountry)
				{
					pCountry->beginTechVote();
				}
			}
			else
			{
				CVoteM::getMe().force_close_vote(rev->dwCountryID,Cmd::TECH_VOTE);
			}

			return true;
		}
		break;
	case Cmd::Session::PARA_CONTRIBUTE_COUNTRY:
		{
			Cmd::Session::t_ContributeCountry_SceneSession* rev =
				(Cmd::Session::t_ContributeCountry_SceneSession*)pNullCmd;

			CCountry* pCountry = find(rev->dwCountry);

			if (pCountry)
			{
				if (rev->byType !=3)
				{
					pCountry->changeMaterial(rev->byType,rev->dwValue);
					pCountry->writeDatabase();
				}
				else if (rev->byType == 3)
				{
					CAlly* pAlly = CAllyM::getMe().findAlly(rev->dwCountry);
					if (pAlly){
						pAlly->changeFriendDegree(rev->dwValue);
						if (0 == pAlly->friendDegree()) {
							CAllyM::getMe().fireAlly(rev->dwCountry,0);  
						}
					}
				}

				pCountry->addGeneralExp(rev->dwValue);
			}

			return true;
		}
		break;
	case Cmd::Session::PARA_VIEW_COUNTRY_DARE:
		{
			Cmd::Session::t_viewCountryDare_SceneSession* rev = 
				(Cmd::Session::t_viewCountryDare_SceneSession*)pNullCmd;

			UserSession* pUser  = UserSessionManager::getInstance()->getUserByID(rev->dwUserID);
			std::vector<CCountry*>::iterator vIterator;

			rwlock.rdlock();

			for (vIterator = countries.begin(); vIterator!=countries.end(); vIterator++)
			{
				if ((*vIterator)->dwDareCountryID>0)
				{
					CCountry* pAttCountry = 
						CCountryM::getMe().find((*vIterator)->dwDareCountryID);

					if (pAttCountry)
					{
						pUser->sendGmChat(Cmd::INFO_TYPE_GAME,"国战记录:%s　挑战 %s",
							pAttCountry->name,(*vIterator)->name);
					}
				}
				else
				{
					pUser->sendGmChat(Cmd::INFO_TYPE_GAME,"国战记录: %s 今天无挑战",(*vIterator)->name);
				}
			}

			rwlock.unlock();  
			return true;
		}
		break;
	case Cmd::Session::PARA_COUNTRY_DARE_RESULT:
		{
			Cmd::Session::t_countryDareResult_SceneSession* rev = 
				(Cmd::Session::t_countryDareResult_SceneSession*)pNullCmd;

			CCountry* pAttCountry = CCountryM::getMe().find(rev->dwAttCountryID);
			CCountry* pDefCountry = CCountryM::getMe().find(rev->dwDefCountryID);

			UserSession* pUser  = UserSessionManager::getInstance()->getUserByID(rev->dwAttUserID);

			if (pAttCountry == NULL || pDefCountry == NULL)    
			{
				return true;
			}

			if (rev->byType == Cmd::Session::COUNTRY_ANNOY_DARE)
			{
				pAttCountry->dwAnnoyWin++;
				pDefCountry->dwAnnoyFail++;

				if (CArmyM::getMe().isCaptain(rev->dwAttUserID))
				{
					pDefCountry->swapMaterialByPer(pAttCountry,3);
				}
				else
				{
					pDefCountry->swapMaterialByPer(pAttCountry,2);
				}

				if (pUser)
				{
					SessionChannel::sendAllInfo(Cmd::INFO_TYPE_EXP,
						" %s %s 砍倒 %s 国旗,%s 成功掠夺 %s 国库百分之三",
						pAttCountry->name,pUser->name,pDefCountry->name,
						pAttCountry->name,pDefCountry->name);
				}
				else
				{
					SessionChannel::sendAllInfo(Cmd::INFO_TYPE_EXP,
						"%s 战胜了 %s,%s 国家物资被 %s 掠夺,%s 3\%的金钱与物资被运到 %s",
						pAttCountry->name,pDefCountry->name,
						pDefCountry->name,pAttCountry->name,
						pDefCountry->name,pAttCountry->name);

				}

				pAttCountry->writeDatabase();
				pDefCountry->writeDatabase();

				//SessionChannel::sendAllInfo(Cmd::INFO_TYPE_SYS,"%s 战胜了 %s",
				//    rev->attCountryName,rev->defCountryName);

				CSeptM::getMe().changeAllRepute(pAttCountry->dwID,4);
				CSeptM::getMe().changeAllRepute(pDefCountry->dwID,-4);
			}
			else if (rev->byType == Cmd::Session::COUNTRY_FORMAL_DARE)
			{
				//TODO:从对战管理器中找到对战对象,进行对战结果计算,并使其进入READY_OVER状态
				CDare* pDare = CDareM::getMe().findDareRecord(Cmd::COUNTRY_FORMAL_DARE,
					rev->dwAttCountryID,rev->dwDefCountryID);

				if (pDare)
				{
					if (pDare->isAtt(rev->dwAttCountryID))
					{//打死大将军的是挑战国
						pDare->grade1 += 2; //挑战国胜,挑战国加两颗星
					}
					else if (pDare->isAtt(rev->dwDefCountryID))
					{
						pDare->grade2 +=2; //反攻成功,防守国加两颗星
					}

					pDare->setReadyOverState();
				}
			}
			else if (rev->byType == Cmd::Session::COUNTRY_FORMAL_ANTI_DARE)
			{
				CDare*  pDare = CDareM::getMe().findDareRecord(Cmd::COUNTRY_FORMAL_ANTI_DARE,                                                            rev->dwAttCountryID,rev->dwDefCountryID);

				if (pDare) {
					if (pDare->isAtt(rev->dwAttCountryID))
					{//打死大将军的是挑战国
						pDare->grade1 += 2; //挑战国胜,挑战国加两颗星
					}
					else if (pDare->isAtt(rev->dwDefCountryID))
					{
						pDare->grade2 +=2; //反攻成功,防守国加两颗星
					}

					pDare->setReadyOverState();
				}
			}
			else if (rev->byType == Cmd::Session::EMPEROR_DARE)
			{
				CDare* pDare = CDareM::getMe().findDareRecord(Cmd::EMPEROR_DARE,6,0);
				pDare->dwWinnerID = rev->dwAttCountryID;
				pDare->setReadyOverState();
			}

			return true;
		}

		break;
	default:
		break;
	}

	return false;
}

void CCountryM::processReqDailyEmperorMoney(UserSession* pUser,Cmd::stReqDailyEmperorMoneyCmd* rev)
{
	CCountry* pEmperor = this->find(NEUTRAL_COUNTRY_ID);
	if (pEmperor)
	{
		if (strncmp(pEmperor->kingName,pUser->name,MAX_NAMESIZE) == 0)
		{
			time_t ct = time(NULL);
			if ((ct-pEmperor->dwLastDailyMoney) >= 24*60*60)
			{
				Cmd::Session::t_dareGold_SceneSession send;
				send.dwUserID = pUser->id;
				send.dwNum = 50000; // 5锭
				send.dwType =  Cmd::Session::EMPEROR_GOLD;
				send.dwWarID = 0;

				if (pUser->scene) 
				{       
					pUser->scene->sendCmd(&send,sizeof(Cmd::Session::t_dareGold_SceneSession));
					Zebra::logger->info("角色 %s 领取了皇帝税金%u文",pUser->name,send.dwNum);
					pEmperor->dwLastDailyMoney = ct;
					pEmperor->writeDatabase();
				}       
			}
			else
			{
				pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"一天只能领取一次奖励.");
			}
		}
		else
		{
			pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"您不是皇帝不能使用该项功能.");
		}
	}
	else
	{
		Zebra::logger->error("[国家]: 中立国数据不完整.");
	}
}

void CCountryM::processUpTech(UserSession* pUser,Cmd::stUpTechDegreeUserCmd* rev)
{
	CCountry* pCountry = this->find(pUser->country);
	if (!pCountry)
	{
		Zebra::logger->error("[国家]:国家信息获取失败,请检查国家信息完整性");
	}

	CTech* pTech = pCountry->getTech(rev->dwOption);
	if (!pTech)
	{
		pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"该项科技不存在,请确认后再升级");
	}

	pTech->upLevel(pUser);
	CCountryM::getMe().broadcastTech(pUser->country);

	Cmd::stReqTechUserCmd send;
	send.dwType = CTech::ACTIVE_TECH;

	CCountryM::getMe().processRequestTech(pUser,&send);
}

void CCountryM::processConfirmSearcher(UserSession* pUser,Cmd::stConfirmSearcherUserCmd* rev)
{
	CCountry* pCountry = this->find(pUser->country);

	if (!pCountry)
	{
		Zebra::logger->error("[国家]:国家信息获取失败,请检查国家信息完整性");
	}

	CTech* pTech = pCountry->getTech(rev->dwOption);
	if (!pTech)
	{
		pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"该项科技不存在,请确认后再升级");
	}

	pTech->setSearcher(pUser);
}

void CCountryM::processReqWaitOfficial(UserSession* pUser,Cmd::stReqWaitOfficialUserCmd* rev)
{
	CCountry* pCountry = this->find(pUser->country);

	if (!pCountry)
	{
		Zebra::logger->error("[国家]:国家信息获取失败,请检查国家信息完整性");
	}

	if (pCountry->isKing(pUser))
	{
		UserSessionManager::getInstance()->sendGraceSort(pUser);
	}
	else
	{
		pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"您不是国王,不能使用该项功能");
	}
}

void CCountryM::processCancelDiplomat(UserSession* pUser,Cmd::stCancelDiplomatCmd* rev)
{
	CCountry* pCountry = this->find(pUser->country);

	if (!pCountry)
	{
		Zebra::logger->error("[国家]:国家信息获取失败,请检查国家信息完整性");
	}

	if (pCountry->isKing(pUser))
	{
		pCountry->cancelDiplomat();
		pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"罢免外交官成功");
	}
}

void CCountryM::processCancelCatcher(UserSession* pUser,Cmd::stCancelCatcherCmd* rev)
{
	CCountry* pCountry = this->find(pUser->country);

	if (!pCountry)
	{
		Zebra::logger->error("[国家]:国家信息获取失败,请检查国家信息完整性");
	}

	if (pCountry->isKing(pUser))
	{
		pCountry->cancelCatcher();
		pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"罢免捕头成功");
	}
}

void CCountryM::processCancelTechSearch(UserSession* pUser,Cmd::stCancelTechSearchUserCmd* rev)
{
	CCountry* pCountry = this->find(pUser->country);

	if (!pCountry)
	{
		Zebra::logger->error("[国家]:国家信息获取失败,请检查国家信息完整性");
	}

	if (pCountry->isKing(pUser))
	{
		CTech* pTech = pCountry->getTech(rev->dwOption);
		if (!pTech)
		{
			pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"该项科技不存在,请确认后再取消");
			return;
		}

		pTech->clearSearcher(pUser);

		Cmd::stReqTechUserCmd send;
		send.dwType = CTech::ACTIVE_TECH;

		CCountryM::getMe().processRequestTech(pUser,&send);
	}
	else
	{
		pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"您不是国王,不能使用该项功能");
	}
}

void CCountryM::processSetTechSearch(UserSession* pUser,Cmd::stSetTechSearchUserCmd* rev)
{
	CCountry* pCountry = this->find(pUser->country);

	if (!pCountry)
	{
		Zebra::logger->error("[国家]:国家信息获取失败,请检查国家信息完整性");
	}

	if (pCountry->isKing(pUser))
	{
		CTech* pTech = pCountry->getTech(rev->dwOption);
		if (!pTech)
		{
			pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"该项科技不存在,请确认后再设置");
			return;
		}

		UserSession* pSearcher  = UserSessionManager::getInstance()->getUserByID(rev->dwCharID);
		if (!pSearcher)
		{
			pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"该玩家已不在线,请另外选择");
			return;
		}

		if (this->isOfficial(pSearcher))
		{
			pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"该玩家已是研究员,请另外选择");
			return;
		}

		Cmd::stConfirmSearcherUserCmd send;
		send.dwOption = pTech->dwType;
		pSearcher->sendCmdToMe(&send,sizeof(send));
		pUser->sendSysChat(Cmd::INFO_TYPE_GAME,"已发送 %s 研究任命书",pTech->szName);
	}
	else
	{
		pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"您不是国王,不能使用该项功能");
	}
}

void CCountryM::processSetDiplomat(UserSession* pUser,Cmd::stAppointDiplomatCmd* rev)
{
	if (this->isKing(pUser))
	{
		UserSession *u = UserSessionManager::getInstance()->getUserSessionByName(rev->name);
		if (!u || !u->scene)
		{
			pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"玩家 %s 不在线",rev->name);
			return;
		}

		if (u->id == pUser->id)
		{
			pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"不能任命自己为外交官");
			return;
		}

		if (u->country != pUser->country)
		{
			pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"只能任命本国人为外交官");
			return;
		}

		if (CCityM::getMe().isCastellan(u) || this->isOfficial(u))
		{
			pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"不能任命城主或研究员为外交官");
			return;
		}

		CCountry* pCountry = this->find(pUser->country);

		if (pCountry)
		{

			if (strncmp(pCountry->diplomatName,rev->name,MAX_NAMESIZE) == 0
				||  strncmp(pCountry->catcherName,rev->name,MAX_NAMESIZE) == 0)
			{
				pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"%s 已为外交官或捕头,不能再任命",rev->name);
				return;
			}

			if (strlen(pCountry->diplomatName)>0)
			{
				pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"已有外交官,不能再任命");
				return;
			}

			if (pCountry->changeDiplomat(u))
			{
				pUser->sendSysChat(Cmd::INFO_TYPE_GAME,"成功任命 %s 为外交官",rev->name);
			}
		}
		else
		{
			Zebra::logger->error("%d 国家数据不存在",pUser->country);
		}
	}
	else
	{
		pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"您不是国王,不能使用该项功能");
	}
}

void CCountryM::processSetCatcher(UserSession* pUser,Cmd::stAppointCatcherCmd* rev)
{
	if (this->isKing(pUser))
	{
		UserSession *u = UserSessionManager::getInstance()->getUserSessionByName(rev->name);
		if (!u || !u->scene)
		{
			pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"玩家 %s 不在线",rev->name);
			return;
		}

		if (u->id == pUser->id)
		{
			pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"不能任命自己为捕头");
			return;
		}

		if (u->country != pUser->country)
		{
			pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"只能任命本国人为捕头");
			return;
		}

		if (CCityM::getMe().isCastellan(u) || this->isOfficial(u))
		{
			pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"不能任命城主或研究员为捕头");
			return;
		}

		CCountry* pCountry = this->find(pUser->country);

		if (pCountry)
		{
			if (strncmp(pCountry->diplomatName,rev->name,MAX_NAMESIZE) == 0
				||  strncmp(pCountry->catcherName,rev->name,MAX_NAMESIZE) == 0)
			{
				pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"%s 已为外交官或捕头,不能再任命",rev->name);
				return;
			}

			if (strlen(pCountry->catcherName)>0)
			{
				pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"已有捕头,不能再任命");
				return;
			}

			Cmd::Session::t_checkUser_SceneSession send;
			send.dwCheckID = pUser->id;
			send.dwCheckedID = u->id;
			u->scene->sendCmd(&send,sizeof(send));
		}
		else
		{
			Zebra::logger->error("%d 国家数据不存在",pUser->country);
		}
	}
	else
	{
		pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"您不是国王,不能使用该项功能");
	}
}

void CCountryM::processDareCountry(UserSession* pUser,Cmd::stDareCountryFormalCmd* rev)
{
	//by RAY
	//pUser->sendSysChat(Cmd::INFO_TYPE_FAIL, "国战系统正在开 发中");

	//#if 0  
	CUnion* pUnion = CUnionM::getMe().getUnionByID(pUser->unionid);

	if (pUser->country == rev->dwCountryID)
	{
		pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"您不能挑战自己的国家");
		return;
	}

	if (!pUnion)
	{
		pUser->sendSysChat(Cmd::INFO_TYPE_MSG,"您不是国王,不能挑战别的国家");
		return;
	}

	if (!pUnion->master)
	{
		pUser->sendSysChat(Cmd::INFO_TYPE_MSG,"您不是国王,不能挑战别的国家");
		return;
	}

	if (pUnion->master->id == pUser->id)
	{// 是帮主,判断是不是国王
		CCity* pCity = CCityM::getMe().find(pUser->country,KING_CITY_ID); // 王城
		// 王城的ID
		CCountry* pCountry = this->find(pUser->country);

		if (pCountry)
		{
			if (pCity && ((pCity->dwUnionID == pUser->unionid) 
				|| pCountry->dwKingUnionID == pUser->unionid))
			{// 是帮主,是王城城主或是国王,允许挑战,取出被挑国家,判断是否在被挑期限
				struct tm tv1,tv2,tv3;  
				time_t timValue = time(NULL);
				zRTime::getLocalTime(tv1,timValue);
				zRTime::getLocalTime(tv3,(timValue+24*60*60));

				/*CDare* pDare = CDareM::getMe().
				findDareRecord(Cmd::COUNTRY_FORMAL_DARE,rev->dwCountryID,
				pUser->country);
				// */

				if (tv3.tm_wday == 6)
				{
					pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,
						"明天将进行皇城争夺战,不允许挑战他国");
					return;
				}

				/*if (pDare != NULL)
				{
				pDare->isAntiAtt = true;
				pDare->sendAntiAtt();

				SessionChannel::sendCountryInfo(Cmd::INFO_TYPE_EXP,
				pUser->country,
				"%d年%d月%d日20点40分,我国向　%s　发起了反攻,请做好进攻准备",
				tv1.tm_year+1900,tv1.tm_mon+1,tv1.tm_mday,
				this->find(rev->dwCountryID)->name);

				return;
				}
				// */

#ifndef _DEBUG
				if (tv1.tm_hour>=18)
				{
					pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,
						"请在18点以前来挑战");
					return;
				}
#endif            
				/*if (pCountry->dwDareCountryID>0)  
				{
				pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"已接受别国挑战！");
				return;
				}*/           

				if (this->find(6,pUser->unionid)!=NULL)
				{
					if (tv3.tm_wday==6)
					{
						pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,
							"明天将进行皇城争夺战,不允许挑战他国");

						return;
					}
				}

				CCountry* pDefCountry = this->find(rev->dwCountryID);
				if (pDefCountry == NULL)
				{
					pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"您挑战的国家不存在");
					return;
				}


				zRTime::getLocalTime(tv2,pDefCountry->dwLastDareTime);
				if ((::abs((long)(tv1.tm_mday - tv2.tm_mday)) >= 3) ||
					::abs((long)(timValue - pDefCountry->dwLastDareTime)) >72*60*60)
				{
					if (pDefCountry->dwDareCountryID>0)
					{
						pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,
							"该国已接受别国挑战。请明天再来。");
						return;
					}
					else
					{  
						/*if (this->findByDare(pDefCountry->dwID,false) != NULL)
						{
						pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,
						"该国已挑战其它国家,不能再对其进行挑战。");
						return;
						}
						// */

						if (this->find(6,pDefCountry->dwKingUnionID) != NULL)
						{
							if (tv3.tm_wday == 6)
							{
								pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,
									"明天将进行皇城争夺战,不允许被挑战");
								return;
							}
						}

						//遍历管理器,看是否已挑战别国
						if (this->findByDare(pUser->country) == NULL)
						{
							rwlock.wrlock();
							pDefCountry->dwDareCountryID = pUser->country;
							pDefCountry->dwLastDareTime = timValue;
							pCountry->dwDareTime = timValue;
							rwlock.unlock();

							pDefCountry->writeDatabase();
							pCountry->writeDatabase();

							/*SessionChannel::sendCountryInfo(Cmd::INFO_TYPE_EXP,
							pDefCountry->dwID,
							"一小时后,　%s　将攻打我国,请做好防御准备",
							pCountry->name);*/

							SessionChannel::sendCountryInfo(Cmd::INFO_TYPE_EXP,
								pDefCountry->dwID,
								"%d年%d月%d日20点40分,　%s　将攻打我国,请做好防御准备",
								tv3.tm_year+1900,tv3.tm_mon+1,tv3.tm_mday,pCountry->name);

							SessionChannel::sendCountryInfo(Cmd::INFO_TYPE_EXP,
								pCountry->dwID,
								"%d年%d月%d日20点40分,　%s　国王将带领我们去攻打 %s。",
								tv3.tm_year+1900,tv3.tm_mon+1,tv3.tm_mday,
								pCountry->name,pDefCountry->name);
							/*SessionChannel::sendCountryInfo(Cmd::INFO_TYPE_EXP,
							pCountry->dwID,
							"一小时后,　%s　国王将带领我们去攻打 %s。",
							pCountry->name,pDefCountry->name);
							*/
							pUser->sendSysChat(Cmd::INFO_TYPE_EXP,
								"发起国战请求成功！请在明天八点四十分准时参加!");

							/*pUser->sendSysChat(Cmd::INFO_TYPE_EXP,
							"发起国战请求成功！请一小时后准时参加!");*/

#ifdef _DEBUG
							//              pDefCountry->beginDare();
#endif                  
						}
						else
						{
							pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,
								"您今天已发起了挑战,不能再挑战了");
						}
					}
				}
				else
				{
					pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"才被挑了,该让别人歇两天了");
				}
			}
			else
			{
				pUser->sendSysChat(Cmd::INFO_TYPE_MSG,"您不是国王,不能挑战别的国家");
			}
		}
		else
		{
			pUser->sendSysChat(Cmd::INFO_TYPE_MSG,"您不是国王,不能挑战别的国家");
			return;
		}
	}
	else
	{
		pUser->sendSysChat(Cmd::INFO_TYPE_MSG,"您不是国王,不能挑战别的国家");
	}
	//#endif
}

void CCountryM::processAntiDareCountry(UserSession* pUser,Cmd::stAntiDareCountryFormalCmd* rev)
{
	if (pUser->country == rev->dwCountryID)
	{
		pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"您不能挑战自己的国家");
		return;
	}

	if (this->isKing(pUser))
	{
		CDareRecord * pRecord = CDareRecordM::getMe().findLastRecord(rev->dwCountryID,pUser->country);
		if (pRecord)
		{
			CCountry* pDefCountry = this->find(rev->dwCountryID);
			if (pDefCountry && CDareM::getMe().findDareRecordByID(Cmd::COUNTRY_FORMAL_DARE,pDefCountry->dwID)==NULL)
			{
				pDefCountry->beginAntiDare(pUser->country);
			}
		}
		else
		{
			pUser->sendSysChat(Cmd::INFO_TYPE_MSG,"不满足反攻条件,不能挑战该国");
			return;
		}
	}
	else
	{
		pUser->sendSysChat(Cmd::INFO_TYPE_MSG,"您不是国王,不能挑战别的国家");
		return;
	}
}

void CCountryM::processRequestTech(UserSession* pUser,Cmd::stReqTechUserCmd* rev)
{
	BYTE buf[zSocket::MAX_DATASIZE];
	Cmd::stTechItem* tempPoint;
	Cmd::stRtnTechUserCmd* retCmd = (Cmd::stRtnTechUserCmd*)buf;
	constructInPlace(retCmd);

	tempPoint = (Cmd::stTechItem *)retCmd->data;
	retCmd->dwType = rev->dwType;
	CCountry* pCountry = this->find(pUser->country);

	if (!pCountry)
	{
		return;
	}

	switch (rev->dwType)
	{
	case  CTech::WAIT_TECH:
		{
			for (int i=1; i<TECH_MAX_NUM; i++)
			{
				CTech* pTech = pCountry->getTech(i);
				if (pTech && pTech->state()==CTech::WAIT_TECH)
				{
					tempPoint->dwOption = pTech->dwType;
					strncpy(tempPoint->szOptionDesc,pTech->szName,MAX_NAMESIZE);
					bzero(tempPoint->szResearchName,sizeof(tempPoint->szResearchName));
					tempPoint->dwLevel = pTech->dwLevel;
					tempPoint->dwProgress = 0;
					tempPoint++;
					retCmd->dwSize++;
				}
			}
		}
		break;
	case CTech::ACTIVE_TECH:
		{
			for (int i=1; i<TECH_MAX_NUM; i++)
			{
				CTech* pTech = pCountry->getTech(i);
				if (pTech && pTech->state()==CTech::ACTIVE_TECH)
				{
					tempPoint->dwOption = pTech->dwType;
					strncpy(tempPoint->szOptionDesc,pTech->szName,MAX_NAMESIZE);
					strncpy(tempPoint->szResearchName,pTech->szResearchName,MAX_NAMESIZE);
					tempPoint->dwLevel = pTech->dwLevel;
					tempPoint->dwProgress = pTech->dwProgress;
					tempPoint++;
					retCmd->dwSize++;
				}
			}
		}
		break;
	case CTech::FINISH_TECH:
		{
			for (int i=1; i<TECH_MAX_NUM; i++)
			{
				CTech* pTech = pCountry->getTech(i);
				if (pTech && pTech->level()>1)
				{
					tempPoint->dwOption = pTech->dwType;
					strncpy(tempPoint->szOptionDesc,pTech->szName,MAX_NAMESIZE);
					strncpy(tempPoint->szResearchName,pTech->szResearchName,MAX_NAMESIZE);
					tempPoint->dwLevel = pTech->dwLevel;
					tempPoint->dwProgress = 0;
					tempPoint++;
					retCmd->dwSize++;
				}
			}
		}
		break;
	default:
		break;
	}

	if (retCmd->dwSize>0)
	{
		pUser->sendCmdToMe(retCmd,(retCmd->dwSize*sizeof(Cmd::stTechItem) + sizeof(Cmd::stRtnTechUserCmd)));
	}
}

void CCountryM::processRequestDare(UserSession* pUser,Cmd::stRequestDareCountryCmd* rev)
{
	BYTE buf[zSocket::MAX_DATASIZE];
	Cmd::stCountryInfo *tempCountry = NULL;

	std::vector<CCountry*>::iterator vIterator;

	rwlock.rdlock();
	Cmd::stReturnDareCountryCmd *retCmd=(Cmd::stReturnDareCountryCmd *)buf;

	constructInPlace(retCmd);
	tempCountry = (Cmd::stCountryInfo *)retCmd->country_list;
	retCmd->byType = rev->byType;
	retCmd->dwSize = 0;

	if (rev->byType == Cmd::REQUEST_BATTLEFIELD_COUNTRY_LIST)
	{
		struct tm tv1;
		time_t timValue = time(NULL);
		zRTime::getLocalTime(tv1,timValue);
		if (tv1.tm_hour <20 || tv1.tm_hour>22)
		{
			pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"非国战时间,不能跳转到战场");
		}
		else
		{
			for (vIterator = countries.begin(); vIterator!=countries.end(); vIterator++)
			{
				if ((*vIterator)->dwDareCountryID>0)
				{
					tempCountry->dwID = (*vIterator)->dwID;
					if (pUser->country == (*vIterator)->dwID 
						|| pUser->country==(*vIterator)->dwDareCountryID)
					{
						tempCountry->byType = 0;
					}
					else
					{
						tempCountry->byType = 1;
					}
					tempCountry++;
					retCmd->dwSize++;
				}
			}
		}  
	}
	else if (rev->byType == Cmd::REQUEST_DARE_COUNTRY_LIST)
	{
		struct tm tv1,tv2;
		time_t timValue = time(NULL);
		zRTime::getLocalTime(tv1,timValue);


		for (vIterator = countries.begin(); vIterator!=countries.end(); vIterator++)
		{

			zRTime::getLocalTime(tv2,(*vIterator)->dwLastDareTime);

			if ((::abs(tv1.tm_mday - tv2.tm_mday) >= 2) || 
				(timValue - (*vIterator)->dwLastDareTime) >48*60*60)
			{
				if ((*vIterator)->dwID != pUser->country && (*vIterator)->dwID !=6)
				{
					tempCountry->dwID = (*vIterator)->dwID;
					tempCountry++;
					retCmd->dwSize++;
				}
			}
		}

	}

	pUser->sendCmdToMe(retCmd,(retCmd->dwSize*sizeof(Cmd::stCountryInfo)+sizeof(Cmd::stReturnDareCountryCmd)));

	rwlock.unlock();
}

void CCountryM::endDare()
{
	std::vector<CCountry*>::iterator vIterator;
	rwlock.rdlock();

	for (vIterator = countries.begin(); vIterator!=countries.end(); vIterator++)
	{
		if ((*vIterator)->dwDareCountryID > 0)
		{
			(*vIterator)->endDare();
		}
	}  
	rwlock.unlock();
}

void CCountryM::beginDare()
{
	std::vector<CCountry*>::iterator vIterator;
	rwlock.rdlock();

	for(vIterator = countries.begin(); vIterator!=countries.end(); vIterator++)
	{
		(*vIterator)->beginDare();
	}  
	rwlock.unlock();
}

void CCountryM::beginGem()
{
	std::vector<CCountry*>::iterator vIterator;
	rwlock.rdlock();

	for(vIterator = countries.begin(); vIterator!=countries.end(); vIterator++)
	{
		CGemM::getMe().addNewGem((*vIterator)->dwID);
	}  
	rwlock.unlock();
}


void CCountryM::userOnline(UserSession *pUser)
{
	if (pUser)
	{
		CDareM::getMe().userOnlineCountry(pUser);
		/*CDare* pDare = NULL;   
		pDare = CDareM::getMe().findDareRecordByID(Cmd::COUNTRY_FORMAL_DARE,pUser->country);
		if (pDare) 
		{
		#ifdef _DEBUG
		Zebra::logger->debug("[国战]:%s 进入国战状态",pUser->name);
		#endif     
		pDare->sendActiveStateToScene(pUser);
		}
		else
		{
		pDare = CDareM::getMe().findDareRecordByID(Cmd::COUNTRY_FORMAL_ANTI_DARE,pUser->country);
		if (pDare)
		{
		pDare->sendActiveStateToScene(pUser);
		}
		}
		*/

		BYTE buf[zSocket::MAX_DATASIZE];        
		Cmd::stCountryStar *tempPoint;

		Cmd::stUpdateCountryStarCmd *retCmd=(Cmd::stUpdateCountryStarCmd *)buf;
		constructInPlace(retCmd);
		tempPoint = (Cmd::stCountryStar *)retCmd->data;

		rwlock.rdlock();

		for (DWORD i=0; i<countries.size(); i++)
		{
			if (countries[i])
			{
				tempPoint->dwCountry = countries[i]->dwID;
				tempPoint->dwStar = countries[i]->getStar();
				tempPoint++;
				retCmd->dwSize++;
			}
		}

		rwlock.unlock();
		pUser->sendCmdToMe(retCmd,(retCmd->dwSize*sizeof(Cmd::stCountryStar)+sizeof(Cmd::stUpdateCountryStarCmd)));

		Cmd::stUpdateCountryKingUserCmd send;
		CCountry* pCountry = this->find(pUser->country);

		if (pCountry)
		{
			strncpy(send.kingName,pCountry->kingName,MAX_NAMESIZE);
			CCountry* pEmperor = this->find(NEUTRAL_COUNTRY_ID);
			CUnion* pUnion = NULL;

			if (pEmperor && pEmperor->dwKingUnionID>0)
			{
				pUnion = CUnionM::getMe().getUnionByID(pEmperor->dwKingUnionID);
				if (pUnion->dwCountryID == pUser->country)
					send.isEmperor = 1;
			}

			pUser->sendCmdToMe(&send,sizeof(send));

			if (strncmp(pCountry->diplomatName,pUser->name,MAX_NAMESIZE) == 0)
			{
				Cmd::Session::t_setDiplomatState_SceneSession send;
				if (pUser->scene) 
				{
					send.byState = 1;
					send.dwUserID = pUser->id;
					pUser->scene->sendCmd(&send,sizeof(send));
				}
			}
			else if (strncmp(pCountry->catcherName,pUser->name,MAX_NAMESIZE) == 0)
			{
				Cmd::Session::t_setCatcherState_SceneSession send;
				if (pUser->scene)
				{
					send.byState = 1;
					send.dwUserID = pUser->id;
					pUser->scene->sendCmd(&send,sizeof(send));
				}
			}
		}
	}
}

void CCountryM::execEveryCountry(countryCallback & cb)
{
	for (DWORD i=0; i<countries.size(); i++)
	{
		if (countries[i])
			cb.exec(countries[i]);
		else
			Zebra::logger->error("国家管理器包含空指针");
	}
}

void CCountryM::refreshTax()
{
	for (DWORD i=0; i<countries.size(); i++)
	{
		if (countries[i])
		{
			SceneSessionManager::getInstance()->notifyCountryTax(countries[i]->dwID,countries[i]->dwTax);
		}      
		else
			Zebra::logger->error("国家管理器包含空指针");
	}
}

void CCountryM::broadcastTech(DWORD dwCountryID)
{
	Cmd::Session::t_updateTech_SceneSession send;
	CCountry* pCountry = this->find(dwCountryID);

	if (pCountry)
	{
		for (int i=1; i<TECH_MAX_NUM; i++)  
		{
			CTech* pTech = pCountry->getTech(i);
			if (pTech)
			{
				send.data[i-1].dwType = pTech->type();
				send.data[i-1].dwLevel = pTech->level();
			}
		}

		send.dwCountryID = dwCountryID;
		SessionTaskManager::getInstance().broadcastScene(&send,sizeof(send));
	}
	else
	{
		Zebra::logger->info("[国家]: %d 国家数据不存在。请检查国家数据完整性",dwCountryID);
	}
}

bool CCountryM::isKing(UserSession* pUser)
{
	CCountry* pCountry = this->find(pUser->country);

	if (pCountry)
	{
		return pCountry->isKing(pUser);  
	}

	return false;
}

bool CCountryM::isEmperor(UserSession* pUser)
{
	CCountry* pCountry = this->find(NEUTRAL_COUNTRY_ID);

	if (pCountry)
	{
		CUnion* pUnion = CUnionM::getMe().getUnionByID(pUser->unionid);

		if (!pUnion)
		{
			return false;
		}

		// TODO:判断是否是城主或国王
		if (pUnion->master && pUnion->master->id == pUser->id)  
		{//是帮主
			if (pUnion->id == pCountry->dwKingUnionID)
			{
				return true;
			}
		}
	}

	return false;
}


bool CCountryM::isOfficial(UserSession* pUser)
{
	CCountry* pCountry = this->find(pUser->country);

	if (pCountry)
	{
		return pCountry->isOfficial(pUser);  
	}

	return false;
}

void CCountryM::refreshTech(SessionTask* scene,DWORD dwCountryID)
{
	Cmd::Session::t_updateTech_SceneSession send;
	CCountry* pCountry = this->find(dwCountryID);

	if (pCountry)
	{
		for (int i=1; i<TECH_MAX_NUM; i++)  
		{
			CTech* pTech = pCountry->getTech(i);
			if (pTech)
			{
				send.data[i-1].dwType = pTech->type();
				send.data[i-1].dwLevel = pTech->level();
			}
		}

		send.dwCountryID = dwCountryID;
		scene->sendCmd(&send,sizeof(send));
	}
	else
	{
		Zebra::logger->info("[国家]: %d 国家数据不存在。请检查国家数据完整性",dwCountryID);
	}
}

/*
* 每天0点清除禁言和关押的标记
*
*/
void CCountryM::clearForbid()
{
	for (std::vector<CCountry*>::iterator it=countries.begin(); it!=countries.end(); it++)
	{
		(*it)->forbidTalk = 0;
		(*it)->sendPrison = 0;
	}
}

/*
* 每天0点清除外交官
*
*/
void CCountryM::clearDiplomat()
{
	for (std::vector<CCountry*>::iterator it=countries.begin(); it!=countries.end(); it++)
	{
		(*it)->cancelDiplomat();
	}
}
/*
* 每天0点清令牌使用次数
*
*/
void CCountryM::resetCallTimes()
{
	for (std::vector<CCountry*>::iterator it=countries.begin(); it!=countries.end(); it++)
	{
		(*it)->calltimes=0;
		(*it)->writeDatabase();
	}
}

/*
* 每天0点清除捕头
*
*/
void CCountryM::clearCatcher()
{
	for (std::vector<CCountry*>::iterator it=countries.begin(); it!=countries.end(); it++)
	{
		(*it)->cancelCatcher();
	}
}
void CCountryM::refreshGeneral(DWORD country)
{
	if (0==country)//0则刷新所有
	{
		for (DWORD i=0; i<countries.size(); i++)
		{
			if (countries[i])
				countries[i]->refreshGeneral();
		}
	}
	else
	{
		if (6==country) return;
		CCountry* pCountry = find(country);
		if (!pCountry) return;

		pCountry->refreshGeneral();
	}

	/*
	SceneSession * scene = SceneSessionManager::getInstance()->getSceneByID((country<<16)+KING_CITY_ID);
	if (scene)
	{
	Cmd::Session::t_refreshGen_SceneSession send;
	send.dwCountryID = country;
	send.level = pCountry->gen_level;
	send.exp = pCountry->gen_exp;
	send.maxExp = pCountry->gen_maxexp;

	scene->sendCmd(&send,sizeof(send));

	pCountry->gen_refreshTime = 0;
	#ifdef _DEBUG
	Zebra::logger->info("刷新大将军 map=%s level=%u",scene->name,pCountry->gen_level);
	#endif
	}
	else
	Zebra::logger->error("刷新大将军时未找到地图 mapID=%u",(country<<16)+KING_CITY_ID);
	*/
}


//------------------------------------------------------------------------------------------------------------


void CCountry::init(DBRecord* rec)
{
	if (rec)
	{
		this->dwID = rec->get("id");
		this->dwKingUnionID  = rec->get("kingunionid");
		this->dwLastDareTime = rec->get("lastdaretime");
		this->dwLastDailyMoney = rec->get("lastdailymoney");
		this->dwDareTime     = rec->get("daretime");
		this->dwFormalWin   = rec->get("formalwin");
		this->dwFormalFail  = rec->get("formalfail");
		this->dwAnnoyWin    = rec->get("annoywin");
		this->dwAnnoyFail   = rec->get("annoyfail");
		this->dwStar      = rec->get("star");
		this->dwDareCountryID = rec->get("darecountryid");
		this->dwTax = rec->get("tax");
		this->qwGold = rec->get("gold");
		this->qwSilk = rec->get("silk");
		this->qwOre = rec->get("ore");
		this->qwBowlder = rec->get("bowlder");
		this->qwWood = rec->get("wood");
		this->qwCoat = rec->get("coat");
		this->qwHerbal = rec->get("herbal");
		this->qwMaterial = rec->get("material");
		this->qwStock = rec->get("stock");
		this->forbidTalk = rec->get("forbidtalk");
		this->sendPrison = rec->get("sendprison");
		this->gen_level = rec->get("gen_level");
		this->gen_exp = rec->get("gen_exp");
		this->gen_maxexp = rec->get("gen_maxexp");
		this->calltimes = rec->get("calltimes");
		this->kingtime = rec->get("kingtime");

		strncpy(this->name,(const char*)rec->get("name"),MAX_NAMESIZE);
		strncpy(this->kingName,(const char*)rec->get("kingname"),MAX_NAMESIZE);
		strncpy(this->diplomatName,(const char*)rec->get("diplomatname"),MAX_NAMESIZE);
		strncpy(this->catcherName,(const char*)rec->get("catchername"),MAX_NAMESIZE);
		strncpy(this->note,(const char*)rec->get("note"),255);
	}
}

/** 
* \brief 更新数据库记录
*/   
void CCountry::writeDatabase()
{
	DBRecord rec,where;

	std::ostringstream oss;

	oss << "id='" << this->dwID << "'";
	where.put("id",oss.str());

	rec.put("kingname",this->kingName);
	rec.put("diplomatname",this->diplomatName);
	rec.put("catchername",this->catcherName);
	rec.put("formalwin",this->dwFormalWin);
	rec.put("formalfail",this->dwFormalFail);
	rec.put("annoywin",this->dwAnnoyWin);
	rec.put("annoyfail",this->dwAnnoyFail);
	rec.put("star",this->dwStar);
	rec.put("lastdaretime",this->dwLastDareTime);
	rec.put("lastdailymoney",this->dwLastDailyMoney);
	rec.put("daretime",this->dwDareTime);
	rec.put("darecountryid",this->dwDareCountryID);
	rec.put("kingunionid",this->dwKingUnionID);
	rec.put("tax",this->dwTax);
	rec.put("gold",this->qwGold);
	rec.put("silk",this->qwSilk);
	rec.put("ore",this->qwOre);
	rec.put("bowlder",this->qwBowlder);
	rec.put("wood",this->qwWood);
	rec.put("coat",this->qwCoat);
	rec.put("herbal",this->qwHerbal);
	rec.put("material",this->qwMaterial);
	rec.put("stock",this->qwStock);
	rec.put("note",this->note);
	rec.put("forbidtalk",this->forbidTalk);
	rec.put("sendprison",this->sendPrison);
	rec.put("gen_level",this->gen_level);
	rec.put("gen_exp",this->gen_exp);
	rec.put("gen_maxexp",this->gen_maxexp);
	rec.put("calltimes",this->calltimes);
	rec.put("kingtime",this->kingtime);

	DBFieldSet* country = SessionService::metaData->getFields("COUNTRY");

	if (country)       
	{       
		connHandleID handle = SessionService::dbConnPool->getHandle();

		if ((connHandleID)-1 == handle)
		{
			Zebra::logger->error("不能获取数据库句柄");
			return;
		}

		if ((connHandleID)-1 != handle)
		{
			SessionService::dbConnPool->exeUpdate(handle,country,&rec,&where);
		}

		SessionService::dbConnPool->putHandle(handle);
	}
	else
	{
		Zebra::logger->error("国家数据保存失败,COUNTRY表不存在");
		return;
	}
}

bool CCountry::insertDatabase()
{
	DBRecord rec;
	rec.put("id",this->dwID);
	rec.put("kingunionid",this->dwKingUnionID);
	rec.put("lastdaretime",this->dwLastDareTime);
	rec.put("daretime",this->dwDareTime);
	rec.put("darecountryid",this->dwDareCountryID);
	rec.put("name",this->name);
	rec.put("kingname",this->kingName);
	rec.put("formalwin",this->dwFormalWin);
	rec.put("formalfail",this->dwFormalFail);
	rec.put("annoywin",this->dwAnnoyWin);
	rec.put("annoyfail",this->dwAnnoyFail);
	rec.put("star",this->dwStar);
	rec.put("gold",this->qwGold);
	rec.put("tax",this->dwTax);
	rec.put("silk",this->qwSilk);
	rec.put("ore",this->qwOre);
	rec.put("bowlder",this->qwBowlder);
	rec.put("wood",this->qwWood);
	rec.put("coat",this->qwCoat);
	rec.put("herbal",this->qwHerbal);
	rec.put("material",this->qwMaterial);
	rec.put("stock",this->qwStock);
	rec.put("note",this->note);
	rec.put("forbidtalk",this->forbidTalk);
	rec.put("sendprison",this->sendPrison);
	rec.put("gen_level",this->gen_level);
	rec.put("gen_exp",this->gen_exp);
	rec.put("gen_maxexp",this->gen_maxexp);



	DBFieldSet* country = SessionService::metaData->getFields("COUNTRY");

	if (country)
	{
		connHandleID handle = SessionService::dbConnPool->getHandle();

		if ((connHandleID)-1 == handle)
		{
			Zebra::logger->error("不能获取数据库句柄");
			return false;
		}
		else
		{
			SessionService::dbConnPool->exeInsert(handle,country,&rec);
		}

		SessionService::dbConnPool->putHandle(handle);
	}
	else
	{
		Zebra::logger->error("国家数据新建失败,COUNTRY表不存在");
		return false;
	}

	return true;
}

void CCountry::loadTechFromDB()
{
	DBFieldSet* tech = SessionService::metaData->getFields("TECH");

	if (tech)
	{       
		connHandleID handle = SessionService::dbConnPool->getHandle();

		if ((connHandleID)-1 == handle)
		{       
			Zebra::logger->error("不能获取数据库句柄");
			return;
		}               

		DBRecordSet* recordset = NULL;
		DBRecord where;   
		std::ostringstream oss; 

		oss << "countryid='" << this->dwID << "'";
		where.put("countryid",oss.str());

		if ((connHandleID)-1 != handle)
		{
			recordset = SessionService::dbConnPool->exeSelect(handle,tech,NULL,&where);
		}

		SessionService::dbConnPool->putHandle(handle);
		if (recordset)
		{
			for (DWORD i=0; i<recordset->size(); i++)
			{
				DBRecord* rec = recordset->get(i);
				CTech* pTech = new CTech();

				if (pTech)
				{
					pTech->init(rec);
				}

				this->addTech(pTech->dwType,pTech);
			}

			SAFE_DELETE(recordset)
		}
	}
	else
	{
		Zebra::logger->error("科技详细数据加载失败,TECH表不存在");
		return;
	}

	return;
}

CTech* CCountry::getTech(DWORD dwType)
{
	CTechMap::iterator pos;
	pos = techIndex.find(dwType);

	if (pos != techIndex.end())
	{
		return pos->second;
	}

	return NULL;
}

void CCountry::addTech(DWORD dwType,CTech* pTech)
{
	techIndex[dwType] = pTech;
}

void CCountry::updateKing(UserSession* pUser)
{
	if (pUser && strncmp(this->kingName,pUser->name,MAX_NAMESIZE)!=0)
	{
		this->dwKingUnionID = pUser->unionid;
		strncpy(this->kingName,pUser->name,MAX_NAMESIZE);

		this->writeDatabase();
	}
}

bool CCountry::changeKing(UserSession* pUser)
{
	if (pUser == NULL)
	{// 清空国王信息
		rwlock.wrlock();
		this->dwKingUnionID = 0;
		bzero(kingName,sizeof(kingName));
		this->writeDatabase();
		rwlock.unlock();
	}
	else
	{
		CUnion* pUnion = CUnionM::getMe().getUnionByID(pUser->unionid);
		if (pUser && pUnion)
		{
			rwlock.wrlock();
			this->dwKingUnionID = pUser->unionid;

			if (pUnion->master)
			{
				strncpy(kingName,pUnion->master->name,MAX_NAMESIZE);
			}

			this->kingtime=SessionTimeTick::currentTime.sec();
			this->writeDatabase();
			rwlock.unlock();
			pUser->sendSysChat(Cmd::INFO_TYPE_EXP,"恭喜您成为 %s 国王",this->name);
			if (pUnion->master) pUnion->master->update_data();
		}
	}

	return true;
}

bool CCountry::changeEmperor(DWORD dwCountryID)
{
	if (this->dwID == NEUTRAL_COUNTRY_ID)
	{
		CCountry* pCountry = CCountryM::getMe().find(dwCountryID);
		CUnion* pUnion = CUnionM::getMe().getUnionByID(pCountry->dwKingUnionID);
		CUnion* pOldUnion = CUnionM::getMe().getUnionByID(this->dwKingUnionID);

		if (pCountry && pUnion)
		{
			rwlock.wrlock();
			this->dwKingUnionID = pCountry->dwKingUnionID;
			if (pUnion->master)
			{
				strncpy(kingName,pUnion->master->name,MAX_NAMESIZE);
			}
			rwlock.unlock();

			this->writeDatabase();

			if (pUnion->master) pUnion->master->update_data();
			if (pOldUnion && pOldUnion->master) pOldUnion->master->update_data();
			DWORD dwMapID = (NEUTRAL_COUNTRY_ID<<16) + 134;
			SceneSession *pScene = SceneSessionManager::getInstance()->getSceneByID(dwMapID);
			if (pScene)
			{
				Cmd::Session::t_setEmperorHold_SceneSession send;
				send.dwCountryID = dwCountryID;
				pScene->sendCmd(&send,sizeof(send));
			}

		}
	}

	return true;
}

bool CCountry::cancelDiplomat()
{
	UserSession *u = UserSessionManager::getInstance()->getUserSessionByName(this->diplomatName);

	if (u && u->scene)
	{
		Cmd::Session::t_setDiplomatState_SceneSession send;
		send.byState = 0;
		send.dwUserID = u->id;
		u->scene->sendCmd(&send,sizeof(send));
	}

	rwlock.wrlock();
	bzero(this->diplomatName,MAX_NAMESIZE);
	rwlock.unlock();
	this->writeDatabase();

	return true;
}

bool CCountry::cancelCatcher()
{
	UserSession *u = UserSessionManager::getInstance()->getUserSessionByName(this->catcherName);

	if (u && u->scene)
	{
		Cmd::Session::t_setCatcherState_SceneSession send;
		send.byState = 0;
		send.dwUserID = u->id;
		u->scene->sendCmd(&send,sizeof(send));
	}

	rwlock.wrlock();
	bzero(this->catcherName,MAX_NAMESIZE);
	rwlock.unlock();
	this->writeDatabase();

	return true;
}

bool CCountry::changeDiplomat(UserSession* pUser)
{
	if (strncmp(pUser->name,this->diplomatName,MAX_NAMESIZE) != 0)
	{
#ifdef _DEBUG
		Zebra::logger->debug("[国家]:%s 被任命为外交官",pUser->name);
#endif    
		UserSession *u = UserSessionManager::getInstance()->getUserSessionByName(this->diplomatName);
		if (u && u->scene)
		{
			Cmd::Session::t_setDiplomatState_SceneSession send;
			send.byState = 0;
			send.dwUserID = u->id;
			u->scene->sendCmd(&send,sizeof(send));
		}

		rwlock.wrlock();
		strncpy(this->diplomatName,pUser->name,MAX_NAMESIZE);
		rwlock.unlock();

		Cmd::Session::t_setDiplomatState_SceneSession send;
		if (pUser->scene) 
		{
			send.byState = 1;
			send.dwUserID = pUser->id;
			pUser->scene->sendCmd(&send,sizeof(send));

			pUser->sendSysChat(Cmd::INFO_TYPE_GAME,"您被国王任命为外交官");
		}
	}

	this->writeDatabase();

	return true;
}

bool CCountry::changeCatcher(UserSession* pUser)
{
	if (strncmp(pUser->name,catcherName,MAX_NAMESIZE) != 0)
	{
		UserSession *u = UserSessionManager::getInstance()->getUserSessionByName(this->catcherName);
		if (u && u->scene)
		{
			Cmd::Session::t_setCatcherState_SceneSession send;
			send.byState = 0;
			send.dwUserID = u->id;
			u->scene->sendCmd(&send,sizeof(send));
		}

		rwlock.wrlock();
		strncpy(this->catcherName,pUser->name,MAX_NAMESIZE);
		rwlock.unlock();

		Cmd::Session::t_setCatcherState_SceneSession send;
		if (pUser->scene) 
		{
			send.byState = 1;
			send.dwUserID = pUser->id;
			pUser->scene->sendCmd(&send,sizeof(send));

			pUser->sendSysChat(Cmd::INFO_TYPE_GAME,"您被国王任命为捕头");
		}
	}

	return true;
}

bool CCountry::isKing(UserSession* pUser)
{
	CUnion* pUnion = CUnionM::getMe().getUnionByID(pUser->unionid);

	if (!pUnion)
	{
		return false;
	}

	// TODO:判断是否是城主或国王
	if (pUnion->master && pUnion->master->id == pUser->id)  
	{//是帮主
		if (CCityM::getMe().find(pUser->country,KING_CITY_ID,pUser->unionid) !=NULL)
		{//是国王
			return true;
		}
	}

	return false;
}

bool CCountry::isOfficial(UserSession* pUser)
{
	CTechMap::iterator pos;

	for (pos = techIndex.begin(); pos!=techIndex.end(); ++pos)
	{
		CTech* pTech = (CTech*)pos->second;
		if (pTech && pTech->dwResearchID == pUser->id)
		{
			return true;
		}
	}

	return false;
}

bool CCountry::isMe(DWORD country)
{
	if (this->dwID==country)
		return true;

	return false;
}

void CCountry::endDare()
{
	/*  CDare* pDare = CDareM::getMe().findDareRecordByRelation(Cmd::COUNTRY_FORMAL_DARE,dwDareCountryID,dwID);

	if (pDare)
	{
	pDare->setReadyOverState();
	}
	*/
#ifndef _DEBUG  
	time_t timValue  = time(NULL);
	if (::abs((long)(timValue-this->dwLastDareTime)) > 24*60*60)
#endif    
	{
		isBeging = false;
		dwDareCountryID = 0;
		this->writeDatabase();
	}
}

void CCountry::beginDare()
{
	if (this->dwID == NEUTRAL_COUNTRY_ID)
	{//如果是中立国,每到周六,开始皇城争夺战
#ifndef _DEBUG  
		struct tm tv1;
		time_t timValue = time(NULL);
		zRTime::getLocalTime(tv1,timValue);
		if (tv1.tm_wday == 6)
#endif    
		{
			std::vector<DWORD> dare_list;
			isBeging = true;  
			Cmd::Session::t_createDare_SceneSession pCmd;
			pCmd.active_time  =  7200;
			pCmd.ready_time   =       1;

			pCmd.relationID2        =       this->dwID;
			pCmd.type               =       Cmd::EMPEROR_DARE;

			CDareM::getMe().createDare_sceneSession(&pCmd,dare_list);
		}
	}
	else
	{
#ifndef _DEBUG  
		time_t timValue  = time(NULL);
		if (dwDareCountryID>0 && !isBeging 
			&& (::abs((long)(timValue-this->dwLastDareTime)) > 24*60*60)
			)
#endif    
		{
			std::vector<DWORD> dare_list;
			dare_list.push_back(this->dwDareCountryID);
			isBeging = true;  
			Cmd::Session::t_createDare_SceneSession pCmd;
			pCmd.active_time  =  7200;
			pCmd.ready_time =       1;
			pCmd.relationID2        =       this->dwID;
			pCmd.type               =       Cmd::COUNTRY_FORMAL_DARE;

			CDareM::getMe().createDare_sceneSession(&pCmd,dare_list);

			CCountry* pCountry = CCountryM::getMe().find(this->dwDareCountryID);
			if (pCountry)
			{
				SessionChannel::sendCountryInfo(Cmd::INFO_TYPE_EXP,this->dwID,
					"%s 攻打 我国 的国战现在开始,注意防守",
					pCountry->name);

				SessionChannel::sendAllInfo(Cmd::INFO_TYPE_GAME,"%s 攻打 %s 的国战现在开始。",
					pCountry->name,
					this->name);
			}

			SessionChannel::sendCountryInfo(Cmd::INFO_TYPE_EXP,this->dwDareCountryID,
				"我国 攻打 %s 的国战现在开始",this->name);

			// TODO:取消掉外交官
			this->cancelDiplomat();
		}
	}
}


/**
* \brief 开始反攻
*
*/
void CCountry::beginAntiDare(DWORD dwAttCountry)
{
	std::vector<DWORD> dare_list;
	dare_list.push_back(dwAttCountry);

	Cmd::Session::t_createDare_SceneSession pCmd;
	pCmd.active_time  =  3600;
	pCmd.ready_time =       1;

	pCmd.relationID2        =       this->dwID;
	pCmd.type               =       Cmd::COUNTRY_FORMAL_ANTI_DARE;

	CDareM::getMe().createDare_sceneSession(&pCmd,dare_list);

	CCountry* pCountry = CCountryM::getMe().find(dwAttCountry);
	if (pCountry)
	{
		SessionChannel::sendCountryInfo(Cmd::INFO_TYPE_EXP,this->dwID,"%s 反攻 我国 的国战现在开始,注意防守",
			pCountry->name);

		SessionChannel::sendAllInfo(Cmd::INFO_TYPE_GAME,"%s 反攻 %s 的国战现在开始。",
			pCountry->name,
			this->name);
	}

	SessionChannel::sendCountryInfo(Cmd::INFO_TYPE_EXP,dwAttCountry,
		"我国 反攻 %s 的国战现在开始",this->name);
}

void CCountry::addTaxMoney(QWORD qwTaxMoney)
{
	this->qwGold+=qwTaxMoney;
	//this->writeDatabase();
}

void CCountry::changeStar(int star)
{
	rwlock.wrlock();
	dwStar = (int)dwStar + star;

	if ((int)dwStar<0)
	{
		dwStar = 0;
	}

	rwlock.unlock();


	BYTE buf[zSocket::MAX_DATASIZE];        
	Cmd::stCountryStar *tempPoint;

	Cmd::stUpdateCountryStarCmd *retCmd=(Cmd::stUpdateCountryStarCmd *)buf;
	constructInPlace(retCmd);
	tempPoint = (Cmd::stCountryStar *)retCmd->data;
	retCmd->dwSize = 1;
	tempPoint->dwCountry = this->dwID;
	tempPoint->dwStar = this->dwStar;

	SessionTaskManager::getInstance().sendCmdToWorld(retCmd,
		(retCmd->dwSize*sizeof(Cmd::stCountryStar)+sizeof(Cmd::stUpdateCountryStarCmd)));
}

DWORD CCountry::getStar()
{
	return dwStar;
}

int CCountry::changeMaterialByPer(int type,float per)
{
	int value = 0;

	switch (type)
	{
	case 0:
		{
			value = (int)(this->qwMaterial * per/100);
		}
		break;
		/*  
		case 1:
		{
		value = (int)(this->qwSilk * per/100);
		}
		break;
		case 2:
		{
		value = (int)(this->qwOre * per/100);
		}
		break;
		case 3:
		{
		value = (int)(this->qwBowlder * per/100);
		}
		break;
		case 4:
		{
		value = (int)(this->qwWood * per/100);
		}
		break;
		case 5:
		{
		value = (int)(this->qwCoat * per/100);
		}
		break;
		case 6:
		{
		value = (int)(this->qwHerbal * per/100);
		}
		break;
		*/
	case 1:
		value = (int)(this->qwStock * per/100);
		break;
	case 2:
		{
			value = (int)(this->qwGold * per/100);
		}
		break;
	default:
		{
		}
	}

	if (value!=0)
	{
		this->changeMaterial(type,value);
	}

	return value;
}

QWORD CCountry::getMaterial(int type)
{
	QWORD value;

	switch (type)
	{
	case 0:
		{
			value = this->qwMaterial;
		}
		break;
	case 1:
		{
			value = this->qwStock;
		}
		break;
	case 2:
		{
			value = this->qwGold;
		}
		break;
	default:
		{
			value = 0;
		}
	}

	return value;
}

void CCountry::changeMaterial(int type,int value)
{
	switch (type)
	{
	case 0:
		{
			if (value<0)
			{
				if ((int)this->qwMaterial>::abs(value))
				{
					this->qwMaterial+=value;
				}
				else
				{
					this->qwMaterial = 0;
				}
			}
			else
			{
				this->qwMaterial += value;
			}
		}
		break;
	case 1:
		{
			if (value<0)
			{
				if ((int)this->qwStock>::abs(value))
				{
					this->qwStock+=value;
				}
				else
				{
					this->qwStock = 0;
				}
			}
			else
			{
				this->qwStock += value;
			}
		}
		break;
	case 2:
		{
			if (value<0)
			{
				if ((int)this->qwGold>::abs(value))
				{
					this->qwGold+=value;
				}
				else
				{
					this->qwGold = 0;
				}
			}
			else
			{
				this->qwGold += value;
			}
		}
		break;
	default:
		break;
	}

	this->writeDatabase();
}

void CCountry::swapMaterialByPer(CCountry* pToCountry,float per)
{
	for (int i=0; i<=2; i++)
	{
		int value = this->changeMaterialByPer(i,-per);
		pToCountry->changeMaterial(i,-value);
	}
}

void CCountry::beginTechVote()
{
	std::vector<CTech*> vote_tech;

	rwlock.wrlock();
	for (int i=1; i<TECH_MAX_NUM; i++)
	{
		CTech* pTech = this->getTech(i);
		if (pTech && pTech->state()!=CTech::ACTIVE_TECH)
		{
			pTech->state(CTech::INIT_TECH);
			vote_tech.push_back(pTech);
		}
	}
	rwlock.unlock();

	if (!CVoteM::getMe().createNewVote(this->dwID,Cmd::TECH_VOTE,vote_tech))
	{
		Zebra::logger->error("[国家]:新建科技投票失败");
	}
}

void CCountry::addGeneralExp(DWORD num)
{
	gen_exp += num;
	while (gen_exp>=gen_maxexp && gen_level<10)
	{
		gen_level++;
		gen_exp -= gen_maxexp;
		gen_maxexp = (gen_level+1)*500;

		refreshGeneral();
		SessionChannel::sendCountryInfo(dwID,Cmd::INFO_TYPE_EXP,"我国大将军王达到 %u 级",gen_level);
		Zebra::logger->info("大将军升级 country=%u level=%u",dwID,gen_level);
	}
	if (gen_exp>gen_maxexp)
		gen_exp = gen_maxexp;
}

void CCountry::generalLevelDown()
{
	if (gen_level<=1) return;

	gen_level--;
	gen_maxexp = (gen_level+1)*500;
	gen_exp = 0;

	//CCountryM::getMe().refreshGeneral(dwID);
	SessionChannel::sendCountryInfo(dwID,Cmd::INFO_TYPE_EXP,"国战失败,大将军王降为 %u 级",gen_level);
	Zebra::logger->info("大将军降级 country=%u level=%u",dwID,gen_level);
}

void CCountry::refreshGeneral()
{
	SceneSession * scene = SceneSessionManager::getInstance()->getSceneByID((dwID<<16)+KING_CITY_ID);
	if (scene)
	{
		Cmd::Session::t_refreshGen_SceneSession send;
		send.dwCountryID = dwID;
		send.level = gen_level;
		send.exp = gen_exp;
		send.maxExp = gen_maxexp;

		scene->sendCmd(&send,sizeof(send));

		gen_refreshTime = 0;
#ifdef _DEBUG
		Zebra::logger->info("刷新大将军 map=%s level=%u",scene->name,gen_level);
#endif
	}
	else
		Zebra::logger->error("刷新大将军时未找到地图 mapID=%u",(dwID<<16)+KING_CITY_ID);
}

//------------------------------------------------------------------------------------------------------------
CTech::CTech()
{
	dwUID = 0;
	bzero(szName,sizeof(szName));
	bzero(szResearchName,sizeof(szResearchName));
	dwCountryID = 0;
	dwType = 0;
	dwProgress = 0;
	dwStatus = 0;
	dwResearchID = 0;
	dwLastUpTime = 0;
}

CTech::~CTech()
{
}

void CTech::init(DBRecord* rec)
{
	if (rec)
	{
		this->dwUID = rec->get("uid");
		this->dwCountryID = rec->get("countryid");
		this->dwType = rec->get("type");
		this->dwProgress = rec->get("progress");
		this->dwResearchID  = rec->get("researchid");
		this->dwStatus = rec->get("status");
		this->dwLevel = rec->get("level");
		this->dwLastUpTime = rec->get("lastuptime");

		strncpy(this->szName,(const char*)rec->get("name"),MAX_NAMESIZE);
		strncpy(this->szResearchName,(const char*)rec->get("researchname"),MAX_NAMESIZE);
	}
}

void CTech::writeDatabase()
{
	DBRecord rec,where;
	std::ostringstream oss;

	oss << "uid='" << this->dwUID << "'";
	where.put("uid",oss.str());

	rec.put("progress",this->dwProgress);
	rec.put("researchid",this->dwResearchID);
	rec.put("researchname",this->szResearchName);
	rec.put("status",this->dwStatus);
	rec.put("level",this->dwLevel);
	rec.put("lastuptime",this->dwLastUpTime);

	DBFieldSet* tech = SessionService::metaData->getFields("TECH");

	if (tech)
	{       
		connHandleID handle = SessionService::dbConnPool->getHandle();

		if ((connHandleID)-1 == handle)
		{
			Zebra::logger->error("不能获取数据库句柄");
			return;
		}

		if ((connHandleID)-1 != handle)
		{
			SessionService::dbConnPool->exeUpdate(handle,tech,&rec,&where);
		}

		SessionService::dbConnPool->putHandle(handle);
	}
	else
	{
		Zebra::logger->error("国家科技数据保存失败,TECH表不存在");
		return;
	}
}

void CTech::upLevel(UserSession* pUser)
{
	zTime cur_time;

	rwlock.rdlock();
	if (this->dwResearchID != pUser->id)
	{
		pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"您不是该项科技的官员,不能对其升级");
	}

#ifndef _DEBUG  
	if (::abs((long)(cur_time.sec()-this->dwLastUpTime))<60*60)
	{
		pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"科技只能一小时升级一次");
	}
#endif  


	if (this->dwStatus != CTech::ACTIVE_TECH)
	{
		pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"科技还未通过投票,进入可研究阶段");
	}

	rwlock.unlock();

	CCountry* pCountry = CCountryM::getMe().find(pUser->country);
	/**** 调低国家科技升级所需资金物资和原料前的情况
	int up_need_base[] = {160,160,640*100};
	//  int up_need_base[] = {2000,2000,5000};

	int up_need_material[8][3] = {{0,0,0}};

	for (int i=1; i<8; i++)
	{
	for (int j=0; j<3; j++)
	{
	up_need_material[i][j] = up_need_base[j] * (int)pow(2,i-1);
	}
	}
	*/
	//调低国家科技升级所需资金物资和原料
	int up_need_base[] = {2000,2000,5000};
	int up_need_material[8][3] = {{0,0,0}};


	for (int i=1; i<8; i++)
	{
		for (int j=0; j<3; j++)
		{
			up_need_material[i][j] = up_need_base[j] * i ;
		}
	}

	bool success = false;
	for (int i=0; i<3; i++)
	{
		QWORD value = pCountry->getMaterial(i);

		if (value>=(QWORD)up_need_material[this->dwLevel][i])
		{
			int cur_need_material = (int)up_need_material[this->dwLevel][i];
			pCountry->changeMaterial(i,-cur_need_material);
			success = true;
		}
		else
		{
			success = false;
			break;
		}
	}

	if (success)
	{
		if (this->dwProgress+2<100)
		{
			rwlock.wrlock();
			this->dwProgress = this->dwProgress + 2;
			this->dwLastUpTime = cur_time.sec();
			rwlock.unlock();
			pUser->sendSysChat(Cmd::INFO_TYPE_EXP,"%s 本次研究成功",this->szName);
		}
		else
		{
			rwlock.wrlock();
			this->dwLastUpTime = 0;
			this->dwLevel++;
			this->dwStatus=CTech::INIT_TECH;
			this->dwResearchID = 0;
			this->dwProgress = 0;
			bzero(this->szResearchName,MAX_NAMESIZE);
			rwlock.unlock();

			SessionChannel::sendCountryInfo(Cmd::INFO_TYPE_EXP,
				pUser->country,"%s 第%d级研制成功",this->szName,this->dwLevel-1);
		}
	}
	else
	{
		pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"国家物资不足,不能升级科技");
	}

	this->writeDatabase();
}

void CTech::setSearcher(UserSession* pUser)
{
	if (this->dwStatus != CTech::WAIT_TECH)
	{
		pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"科技还未通过投票,不能设置研究员");
		return;
	}

	rwlock.wrlock();
	this->dwResearchID = pUser->id;
	strncpy(this->szResearchName,pUser->name,MAX_NAMESIZE);
	this->dwStatus = CTech::ACTIVE_TECH;
	rwlock.unlock();
	pUser->sendSysChat(Cmd::INFO_TYPE_EXP,"恭喜您成为 %s 研究员",this->szName);

	this->writeDatabase();

}

void CTech::clearSearcher(UserSession* pUser)
{
	if (this->dwResearchID<=0)
	{
		pUser->sendSysChat(Cmd::INFO_TYPE_FAIL,"科技不在研究期间或没有研究员,不能取消研究员");
		return;
	}

	rwlock.wrlock();
	this->dwResearchID = 0;
	bzero(this->szResearchName,MAX_NAMESIZE);
	this->dwStatus = CTech::WAIT_TECH;
	rwlock.unlock();

	this->writeDatabase();
}

