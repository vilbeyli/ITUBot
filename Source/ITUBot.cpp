#include "ITUBot.h"
using namespace BWAPI;

bool analyzed;
bool analysis_just_finished;
BWTA::Region* home;
BWTA::Region* enemy_base;

Unit* chokeGuardian = NULL; 
BWTA::Chokepoint* choke=NULL;


UnitType drawWhat;
TilePosition drawPos;
bool draw = false;
bool builderFound = false;

void guardChoke(Unit*);
void back2work(Unit*);
void populateBuildOrder(std::queue<BWAPI::UnitType>& bo);
TilePosition getBuildTile(Unit* u, UnitType b, Position p);

void ITUBot::onStart(){
	Broodwar->sendText("Hello world!");
	Broodwar->printf("The map is %s, a %d player map",Broodwar->mapName().c_str(),Broodwar->getStartLocations().size());
	
	// Enable some cheat flags
	Broodwar->enableFlag(Flag::UserInput);
	// Uncomment to enable complete map information
	//Broodwar->enableFlag(Flag::CompleteMapInformation);

	//read map information into BWTA so terrain analysis can be done in another thread
	BWTA::readMap();
	analyzed=false;
	analysis_just_finished=false;

	show_bullets=false;
	show_visibility_data=false;

	// if its a replay
	if (Broodwar->isReplay()){
		Broodwar->printf("The following players are in this replay:");
		for(std::set<Player*>::iterator p=Broodwar->getPlayers().begin();p!=Broodwar->getPlayers().end();p++){
			if (!(*p)->getUnits().empty() && !(*p)->isNeutral()){
				Broodwar->printf("%s, playing as a %s",(*p)->getName().c_str(),(*p)->getRace().getName().c_str());
			}
		}
	}

	// if its the actual game
	else{
		Broodwar->printf("The match up is %s v %s",
		Broodwar->self()->getRace().getName().c_str(),
		Broodwar->enemy()->getRace().getName().c_str());

		// start map analysis
		//Broodwar->pauseGame();
		Broodwar->printf("Analyzing map... this may take a minute");
		CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)AnalyzeThread, NULL, 0, NULL);

		//send each worker to the mineral field that is closest to it
		for(std::set<Unit*>::const_iterator i=Broodwar->self()->getUnits().begin();i!=Broodwar->self()->getUnits().end();i++){
			if ((*i)->getType().isWorker())
				back2work(*i);

			else if ((*i)->getType().isResourceDepot()){
			
				//if this is a center, tell it to build the appropiate type of worker
				if ((*i)->getType().getRace()!=Races::Zerg)
					(*i)->train(Broodwar->self()->getRace().getWorker());

			}	
		}

		// populate build order - hardcoded
		populateBuildOrder(buildOrder());

	}	// isReplay closure
}


void ITUBot::onEnd(bool isWinner){
	if (isWinner){
		//log win to file
	}
}

void ITUBot::onFrame(){
	if (show_visibility_data)
		drawVisibilityData();

	if (show_bullets)
		drawBullets();

	if (Broodwar->isReplay())
		return;

	drawStats();

	if (analyzed)
		drawTerrainData();

	if (analysis_just_finished){
		Broodwar->printf("Finished analyzing map.");
		analysis_just_finished=false;
	}

	/*
	//order one of our workers to guard our chokepoint.
	if (analyzed && Broodwar->getFrameCount()%30==0 && chokeGuardian == NULL){
		for(std::set<Unit*>::const_iterator i=Broodwar->self()->getUnits().begin();i!=Broodwar->self()->getUnits().end();i++){
			if ((*i)->getType().isWorker() && (*i)->exists()){
				chokeGuardian = *i;
				//guardChoke(*i);
				break;
			}
		}
	}

	if (chokeGuardian != NULL && (chokeGuardian->isIdle() || chokeGuardian->getDistance(choke->getCenter()) == 0) )
		chokeGuardian->holdPosition();   
	*/
	 

	///////////////////////////////// UNIT COMMAND
	// Iterate through all the units that we own
	const std::set<Unit*> myUnits = Broodwar->self()->getUnits();
	for ( std::set<Unit*>::const_iterator u = myUnits.begin(); u != myUnits.end(); ++u ){

		// If the unit is a worker unit
		if ( (*u)->getType().isWorker() ){
			if ( (*u)->isIdle() ){  // if our worker is idle

				// Order workers carrying a resource to return them to the center,
				// otherwise find a mineral patch to harvest.
				if ( (*u)->isCarryingGas() || (*u)->isCarryingMinerals() )
					(*u)->returnCargo();
				else
					back2work(*u);
			} // closure: if idle
		}

		// If the build order is empty, keep making SCVs and Supply Depots
		if( buildOrder().empty() == true){

			// A resource depot is a Command Center, Nexus, or Hatchery
			if ( (*u)->getType().isResourceDepot() ){

				// Order the depot to construct more workers! But only when it is idle.
				if ( (*u)->isIdle() && !(*u)->train((*u)->getType().getRace().getWorker()) ){

					// If that fails, draw the error at the location so that you can visibly see what went wrong!
					// However, drawing the error once will only appear for a single frame
					// so create an event that keeps it on the screen for some frames
					Position pos = (*u)->getPosition();
					Error lastErr = Broodwar->getLastError();

					// Retrieve the supply provider type in the case that we have run out of supplies
					UnitType supplyProviderType = (*u)->getType().getRace().getSupplyProvider();
					//UnitType supplyProviderType = (*u)->getType().getRace().getRefinery();
					
					static int lastChecked = 0;

					// If we are supply blocked and haven't tried constructing more recently
					if (  lastErr == Errors::Insufficient_Supply &&
						  lastChecked + 400 < Broodwar->getFrameCount() &&
						  Broodwar->self()->incompleteUnitCount(supplyProviderType) == 0 )
					{
						lastChecked = Broodwar->getFrameCount();

						Unit* supplyBuilder = NULL;
						for(std::set<Unit*>::const_iterator i=Broodwar->self()->getUnits().begin();i!=Broodwar->self()->getUnits().end();i++){
							if ( (*i)->getType().isWorker() && (*i)->isCarryingMinerals() ){
								supplyBuilder = (*i);
								break;
							}
						}
					

						// If a unit was found
						if ( supplyBuilder ) {            
							if ( supplyProviderType.isBuilding() ){
								
								// if the map analysis is completed
								if(home != NULL){
									TilePosition targetBuildLocation = getBuildTile(supplyBuilder, supplyProviderType, home->getCenter()); 
									
									if ( targetBuildLocation.x() != -1 && targetBuildLocation.y() != -1 ){

										// draw the layout
										draw = true;
										drawPos = targetBuildLocation;
										drawWhat = supplyProviderType;
										
										// Order the builder to construct the supply structure
										supplyBuilder->build( targetBuildLocation, supplyProviderType );
									}
								}

							}
						} // closure: supplyBuilder is valid

					} // closure: insufficient supply

				} // closure: failed to train idle unit
			
			}
		}	// closure: BUILD ORDER EMPTY


		// build order execution goes here
		else{
			//Broodwar->printf("%s(%d) - Unit ID: %d || %d", buildOrder().front().getName().c_str(), buildOrder().size(), (*u)->getID(), builderFound);
			// if the next unit is a worker in the build order and the current unit is command center
			if( buildOrder().front() == UnitTypes::Terran_SCV  &&
				(*u)->getType().isResourceDepot() &&
				Broodwar->self()->supplyUsed() != Broodwar->self()->supplyTotal() &&
				Broodwar->self()->minerals() >= 50
				)
			{
				if ( (*u)->train(UnitTypes::Terran_SCV) == true )
					buildOrder().pop();
			}

			// if the next unit is Barracks and the current unit is a worker
			else if( buildOrder().front() == UnitTypes::Terran_Barracks &&
				(*u)->getType().isWorker() && !(*u)->isConstructing() && (*u)->isCarryingMinerals() &&
				Broodwar->self()->minerals() >= UnitTypes::Terran_Barracks.mineralPrice() &&
				home != NULL
				) 
			{
				//Broodwar->printf("BARRACKS: SCV %d is not constructing and Minerals! %d", (*u)->getID(), builderFound);
				
				// find a suitable location
				TilePosition targetBuildLocation = getBuildTile(*u, UnitTypes::Terran_Barracks, home->getCenter());
				if ( targetBuildLocation.x() != -1 && targetBuildLocation.y() != -1 ){

					// draw the layout
					draw = true;
					drawPos = targetBuildLocation;
					drawWhat = UnitTypes::Terran_Barracks;
					
					// Order the builder to construct the supply structure
					if (builderFound == false){
						if ( (*u)->build( targetBuildLocation, UnitTypes::Terran_Barracks ) )
							builderFound = true;
						else
							Broodwar->printf("Barracks build (%d, %d) failed.", targetBuildLocation.x(), targetBuildLocation.y());
					}
				}
				else if (targetBuildLocation.x() == -1 || targetBuildLocation.y() == -1) {
					Broodwar->printf("No suitable location found.");
				}
				

			}  // closure : barracks


			// if the next unit is Supply Depot and the current unit is a worker
			else if( buildOrder().front() == UnitTypes::Terran_Supply_Depot &&
				(*u)->getType().isWorker() && !(*u)->isConstructing() && 
				Broodwar->self()->minerals() >= UnitTypes::Terran_Supply_Depot.mineralPrice() &&
				home != NULL
				) 
			{
				
				// find a suitable location
				TilePosition targetBuildLocation = getBuildTile(*u, UnitTypes::Terran_Supply_Depot, home->getCenter());
				if ( targetBuildLocation.x() != -1 && targetBuildLocation.y() != -1 ){

					// draw the layout
					draw = true;
					drawPos = targetBuildLocation;
					drawWhat = UnitTypes::Terran_Supply_Depot;
					
					// Order the builder to construct the supply structure
					if (builderFound == false){
						if( (*u)->build( targetBuildLocation, UnitTypes::Terran_Supply_Depot ) == true)
							builderFound = true;
						else
							Broodwar->printf("Supply Depot build (%d, %d) failed.", targetBuildLocation.x(), targetBuildLocation.y());
					}
					else if (targetBuildLocation.x() == -1 || targetBuildLocation.y() == -1) {
						Broodwar->printf("No suitable location found.");
					}
				}

			}  // closure : supply depot


			// if the next unit is a Marine and current unit is Barracks
			else if ( buildOrder().front() == UnitTypes::Terran_Marine &&
				(*u)->getType() == UnitTypes::Terran_Barracks &&
				Broodwar->self()->supplyUsed() != Broodwar->self()->supplyTotal() &&
				Broodwar->self()->minerals() >= UnitTypes::Terran_Marine.mineralPrice()
				) 
			{
				if ( (*u)->train(UnitTypes::Terran_Marine) == true )
					buildOrder().pop();
			}

		}

	} // closure: unit iterator
}

void ITUBot::onSendText(std::string text)
{
	if (text=="/show bullets")
	{
		show_bullets = !show_bullets;
	} else if (text=="/show players")
	{
		showPlayers();
	} else if (text=="/show forces")
	{
		showForces();
	} else if (text=="/show visibility")
	{
		show_visibility_data=!show_visibility_data;
	} else if (text=="/analyze")
	{ /*
	if (analyzed == false)
	{
	  Broodwar->printf("Analyzing map... this may take a minute");
	  CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)AnalyzeThread, NULL, 0, NULL);
	} */
	} else
	{
		Broodwar->printf("You typed '%s'!",text.c_str());
		Broodwar->sendText("%s",text.c_str());
	}
}

void ITUBot::onReceiveText(BWAPI::Player* player, std::string text)
{
	Broodwar->printf("%s said '%s'", player->getName().c_str(), text.c_str());
}

void ITUBot::onPlayerLeft(BWAPI::Player* player)
{
  Broodwar->sendText("%s left the game.",player->getName().c_str());
}

void ITUBot::onNukeDetect(BWAPI::Position target)
{
  if (target!=Positions::Unknown)
    Broodwar->printf("Nuclear Launch Detected at (%d,%d)",target.x(),target.y());
  else
    Broodwar->printf("Nuclear Launch Detected");
}

void ITUBot::onUnitDiscover(BWAPI::Unit* unit)
{
  if (!Broodwar->isReplay() && Broodwar->getFrameCount()>1);
    //Broodwar->sendText("A %s [%x] has been discovered at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
}

void ITUBot::onUnitEvade(BWAPI::Unit* unit)
{
  if (!Broodwar->isReplay() && Broodwar->getFrameCount()>1)
    Broodwar->sendText("A %s [%x] was last accessible at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
}

void ITUBot::onUnitShow(BWAPI::Unit* unit)
{
  if (!Broodwar->isReplay() && Broodwar->getFrameCount()>1);
    //Broodwar->sendText("A %s [%x] has been spotted at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
}

void ITUBot::onUnitHide(BWAPI::Unit* unit)
{
  if (!Broodwar->isReplay() && Broodwar->getFrameCount()>1)
    Broodwar->sendText("A %s [%x] was last seen at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
}

void ITUBot::onUnitCreate(BWAPI::Unit* unit)
{
	if (Broodwar->getFrameCount()>1)
	{
		if (!Broodwar->isReplay()){
			Broodwar->sendText("A %s [%x] has been created at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
			if(_buildOrder.empty() == false && unit->getType() == _buildOrder.front()){
				if( unit->getType().isBuilding() && unit->getType().isResourceDepot() == false ){
					builderFound = false;
					_buildOrder.pop();
				}
			}

		}
	
		else
		{
			/*if we are in a replay, then we will print out the build order
			(just of the buildings, not the units).*/
			if (unit->getType().isBuilding() && unit->getPlayer()->isNeutral()==false)
			{
				int seconds=Broodwar->getFrameCount()/24;
				int minutes=seconds/60;
				seconds%=60;
				Broodwar->sendText("%.2d:%.2d: %s creates a %s",minutes,seconds,unit->getPlayer()->getName().c_str(),unit->getType().getName().c_str());
			}
		}
	}
}

void ITUBot::onUnitDestroy(BWAPI::Unit* unit)
{
  if (!Broodwar->isReplay() && Broodwar->getFrameCount()>1)
    Broodwar->sendText("A %s [%x] has been destroyed at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
  Broodwar->sendText("DEAD");
  if(unit == chokeGuardian){
    Broodwar->sendText("Guardian Died.");
    chokeGuardian = NULL;
  }
}

void ITUBot::onUnitMorph(BWAPI::Unit* unit)
{
  if (!Broodwar->isReplay())
    Broodwar->sendText("A %s [%x] has been morphed at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
  else
  {
    /*if we are in a replay, then we will print out the build order
    (just of the buildings, not the units).*/
    if (unit->getType().isBuilding() && unit->getPlayer()->isNeutral()==false)
    {
      int seconds=Broodwar->getFrameCount()/24;
      int minutes=seconds/60;
      seconds%=60;
      Broodwar->sendText("%.2d:%.2d: %s morphs a %s",minutes,seconds,unit->getPlayer()->getName().c_str(),unit->getType().getName().c_str());
    }
  }
}

void ITUBot::onUnitRenegade(BWAPI::Unit* unit)
{
  if (!Broodwar->isReplay())
    Broodwar->sendText("A %s [%x] is now owned by %s",unit->getType().getName().c_str(),unit,unit->getPlayer()->getName().c_str());
}

void ITUBot::onSaveGame(std::string gameName)
{
  Broodwar->printf("The game was saved to \"%s\".", gameName.c_str());
}

DWORD WINAPI AnalyzeThread()
{
  BWTA::analyze();

  //self start location only available if the map has base locations
  if (BWTA::getStartLocation(BWAPI::Broodwar->self())!=NULL)
  {
    home       = BWTA::getStartLocation(BWAPI::Broodwar->self())->getRegion();
  }
  //enemy start location only available if Complete Map Information is enabled.
  if (BWTA::getStartLocation(BWAPI::Broodwar->enemy())!=NULL)
  {
    enemy_base = BWTA::getStartLocation(BWAPI::Broodwar->enemy())->getRegion();
  }
  analyzed   = true;
  analysis_just_finished = true;
  //Broodwar->resumeGame();
  return 0;
}

void ITUBot::drawStats()
{
  // Display the game frame rate as text in the upper left area of the screen
  Broodwar->drawTextScreen(200, 0,  "FPS: %d", Broodwar->getFPS() );
  Broodwar->drawTextScreen(200, 20, "Average FPS: %f", Broodwar->getAverageFPS() );

  std::set<Unit*> myUnits = Broodwar->self()->getUnits();
  Broodwar->drawTextScreen(5,0,"I have %d units:",myUnits.size());
  std::map<UnitType, int> unitTypeCounts;
  for(std::set<Unit*>::iterator i=myUnits.begin();i!=myUnits.end();i++)
  {
    if (unitTypeCounts.find((*i)->getType())==unitTypeCounts.end())
    {
      unitTypeCounts.insert(std::make_pair((*i)->getType(),0));
    }
    unitTypeCounts.find((*i)->getType())->second++;
  }
  int line=1;
  for(std::map<UnitType,int>::iterator i=unitTypeCounts.begin();i!=unitTypeCounts.end();i++)
  {
    Broodwar->drawTextScreen(5,16*line,"- %d %ss",(*i).second, (*i).first.getName().c_str());
    line++;
  }
}

void ITUBot::drawBullets()
{
  std::set<Bullet*> bullets = Broodwar->getBullets();
  for(std::set<Bullet*>::iterator i=bullets.begin();i!=bullets.end();i++)
  {
    Position p=(*i)->getPosition();
    double velocityX = (*i)->getVelocityX();
    double velocityY = (*i)->getVelocityY();
    if ((*i)->getPlayer()==Broodwar->self())
    {
      Broodwar->drawLineMap(p.x(),p.y(),p.x()+(int)velocityX,p.y()+(int)velocityY,Colors::Green);
      Broodwar->drawTextMap(p.x(),p.y(),"\x07%s",(*i)->getType().getName().c_str());
    }
    else
    {
      Broodwar->drawLineMap(p.x(),p.y(),p.x()+(int)velocityX,p.y()+(int)velocityY,Colors::Red);
      Broodwar->drawTextMap(p.x(),p.y(),"\x06%s",(*i)->getType().getName().c_str());
    }
  }
}

void ITUBot::drawVisibilityData()
{
  for(int x=0;x<Broodwar->mapWidth();x++)
  {
    for(int y=0;y<Broodwar->mapHeight();y++)
    {
      if (Broodwar->isExplored(x,y))
      {
        if (Broodwar->isVisible(x,y))
          Broodwar->drawDotMap(x*32+16,y*32+16,Colors::Green);
        else
          Broodwar->drawDotMap(x*32+16,y*32+16,Colors::Blue);
      }
      else
        Broodwar->drawDotMap(x*32+16,y*32+16,Colors::Red);
    }
  }
}

void ITUBot::drawTerrainData(){

	//we will iterate through all the base locations, and draw their outlines.
	for(std::set<BWTA::BaseLocation*>::const_iterator i=BWTA::getBaseLocations().begin();i!=BWTA::getBaseLocations().end();i++){
		TilePosition p=(*i)->getTilePosition();
		Position c=(*i)->getPosition();

		//draw outline of center location
		Broodwar->drawBox(CoordinateType::Map,p.x()*32,p.y()*32,p.x()*32+4*32,p.y()*32+3*32,Colors::Blue,false);

		//draw a circle at each mineral patch
		for(std::set<BWAPI::Unit*>::const_iterator j=(*i)->getStaticMinerals().begin();j!=(*i)->getStaticMinerals().end();j++)
		{
			Position q=(*j)->getInitialPosition();
			Broodwar->drawCircle(CoordinateType::Map,q.x(),q.y(),30,Colors::Cyan,false);
		}

		//draw the outlines of vespene geysers
		for(std::set<BWAPI::Unit*>::const_iterator j=(*i)->getGeysers().begin();j!=(*i)->getGeysers().end();j++)
		{
			TilePosition q=(*j)->getInitialTilePosition();
			Broodwar->drawBox(CoordinateType::Map,q.x()*32,q.y()*32,q.x()*32+4*32,q.y()*32+2*32,Colors::Orange,false);
		}

		//if this is an island expansion, draw a yellow circle around the base location
		if ((*i)->isIsland())
			Broodwar->drawCircle(CoordinateType::Map,c.x(),c.y(),80,Colors::Yellow,false);
	}

	//we will iterate through all the regions and draw the polygon outline of it in green.
	for(std::set<BWTA::Region*>::const_iterator r=BWTA::getRegions().begin();r!=BWTA::getRegions().end();r++){
		BWTA::Polygon p=(*r)->getPolygon();
		for(int j=0;j<(int)p.size();j++){
			Position point1=p[j];
			Position point2=p[(j+1) % p.size()];
			Broodwar->drawLine(CoordinateType::Map,point1.x(),point1.y(),point2.x(),point2.y(),Colors::Green);
		}
	}

	//we will visualize the chokepoints with red lines
	for(std::set<BWTA::Region*>::const_iterator r=BWTA::getRegions().begin();r!=BWTA::getRegions().end();r++){
		for(std::set<BWTA::Chokepoint*>::const_iterator c=(*r)->getChokepoints().begin();c!=(*r)->getChokepoints().end();c++){
			Position point1=(*c)->getSides().first;
			Position point2=(*c)->getSides().second;
			Broodwar->drawLine(CoordinateType::Map,point1.x(),point1.y(),point2.x(),point2.y(),Colors::Red);
		}
	}

	// draw the building layout that is about to be built
	if(draw == true){
		if(drawWhat.getName() == "Terran Supply Depot"){
			Broodwar->drawBoxMap(drawPos.x()*32, drawPos.y()*32, drawPos.x()*32+3*32, drawPos.y()*32+2*32, Colors::Green, false);
		}
		if(drawWhat.getName() == "Terran Barracks"){
			Broodwar->drawBoxMap(drawPos.x()*32, drawPos.y()*32, drawPos.x()*32+4*32, drawPos.y()*32+3*32, Colors::Red, false);
		}
	}

}

void ITUBot::showPlayers()
{
  std::set<Player*> players=Broodwar->getPlayers();
  for(std::set<Player*>::iterator i=players.begin();i!=players.end();i++)
  {
    Broodwar->printf("Player [%d]: %s is in force: %s",(*i)->getID(),(*i)->getName().c_str(), (*i)->getForce()->getName().c_str());
  }
}

void ITUBot::showForces()
{
  std::set<Force*> forces=Broodwar->getForces();
  for(std::set<Force*>::iterator i=forces.begin();i!=forces.end();i++)
  {
    std::set<Player*> players=(*i)->getPlayers();
    Broodwar->printf("Force %s has the following players:",(*i)->getName().c_str());
    for(std::set<Player*>::iterator j=players.begin();j!=players.end();j++)
    {
      Broodwar->printf("  - Player [%d]: %s",(*j)->getID(),(*j)->getName().c_str());
    }
  }
}

void ITUBot::onUnitComplete(BWAPI::Unit *unit){
	if (!Broodwar->isReplay() && Broodwar->getFrameCount()>1)
		Broodwar->sendText("A %s [%x] has been completed at (%d,%d)",
							unit->getType().getName().c_str(),
							unit,unit->getPosition().x(),
							unit->getPosition().y()
						);
	
	if(unit->getType().isBuilding() == true){
		;//draw = false;
	}
}


void guardChoke(Unit* u){
    //get the chokepoints linked to our home region
    std::set<BWTA::Chokepoint*> chokepoints= home->getChokepoints();
    double min_length=10000;

    //iterate through all chokepoints and look for the one with the smallest gap (least width)
    for(std::set<BWTA::Chokepoint*>::iterator c=chokepoints.begin();c!=chokepoints.end();c++)
    {
      double length=(*c)->getWidth();
      if (length<min_length || choke==NULL)
      {
        min_length=length;
        choke=*c;
      }
    }

    //order the worker to move to the center of the gap
    u->rightClick(choke->getCenter());
    return;
}

void back2work(Unit* u){
	  Unit* closestMineral=NULL;
	  for(std::set<Unit*>::iterator m=Broodwar->getMinerals().begin();m!=Broodwar->getMinerals().end();m++){
			if (closestMineral==NULL || u->getDistance(*m)< u->getDistance(closestMineral))
				closestMineral=*m;
	  }
	  if (closestMineral!=NULL)
			u->rightClick(closestMineral);
	  
	  return;
}



TilePosition getBuildTile(Unit* u, UnitType b, Position p){
	int x =	p.x(); int y = p.y();
	TilePosition ret(-1, -1);
	int maxDist = 3;
	int stopDist = 50;
	int tileX = x/32; int tileY = y/32;

	// if Refinery is being built
	if( b.isRefinery() ){
		std::set<Unit*>::iterator n = Broodwar->getGeysers().begin();
		for( ; n != Broodwar->getGeysers().end() ; n++){	// iterate through gaysers
			if( (abs( (*n)->getTilePosition().x()-tileX ) < stopDist ) &&			
				(abs( (*n)->getTilePosition().y()-tileY ) < stopDist )
			){
				ret.x() = (*n)->getTilePosition().x();
				ret.y() = (*n)->getTilePosition().y();
				return ret;
			}	
		}
	}


	while( (maxDist < stopDist) && (ret.x() == -1) ){
		for (int i = tileX - maxDist ; i <= tileX + maxDist ; ++i){
			for(int j = tileY - maxDist ; j <= tileY + maxDist ; ++j){
				if( Broodwar->canBuildHere(u, TilePosition(i,j), b, false) ){
					
					//units that are blocking the title
					bool unitsInWay = false;
					for(std::set<Unit*>::const_iterator it = Broodwar->self()->getUnits().begin();
						it != Broodwar->self()->getUnits().end() ; it++){
						
						if( (*it)->getID() == u->getID() )	continue;
						if( (abs((*it)->getTilePosition().x() - i) < 4) && (abs((*it)->getTilePosition().y() - j) < 4) )
							unitsInWay = true;

						if(unitsInWay == false){
							ret.x() = i;	ret.y() = j;
							return ret;
						}
					}
				}
			}

		}
		maxDist += 2;
	}

	if(ret.x() == -1)
		Broodwar->printf("Unable to find suitable build for position %s", b.getName());
	
	return ret;
}
void populateBuildOrder(std::queue<BWAPI::UnitType>& bo){
	bo.push(UnitTypes::Terran_SCV);
	bo.push(UnitTypes::Terran_SCV);
	bo.push(UnitTypes::Terran_SCV);
	bo.push(UnitTypes::Terran_SCV);
	bo.push(UnitTypes::Terran_Barracks);
	bo.push(UnitTypes::Terran_SCV);
	bo.push(UnitTypes::Terran_Supply_Depot);
	bo.push(UnitTypes::Terran_SCV);
	bo.push(UnitTypes::Terran_SCV);
	bo.push(UnitTypes::Terran_Supply_Depot);
	bo.push(UnitTypes::Terran_SCV);
	bo.push(UnitTypes::Terran_Marine);
	bo.push(UnitTypes::Terran_SCV);	  
	bo.push(UnitTypes::Terran_Marine);	 
	bo.push(UnitTypes::Terran_Marine);
	bo.push(UnitTypes::Terran_Supply_Depot);
	bo.push(UnitTypes::Terran_Refinery);

	Broodwar->printf("Build order populated. Size = %d", bo.size());
}