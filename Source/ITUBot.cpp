#include "ITUBot.h"

#include <cassert>
#include <cstdlib>
#include <iostream>	
#include <fstream>
#include <string>
#include <sstream>

#include <errno.h>
#include <cctype>

using namespace BWAPI;
using std::endl;

#define GAP

///////////////////// GLOBAL VARIABLES

// map analysis variables
bool optimizeGap = true;

bool analyzed;
bool analysis_just_finished = false;
BWTA::Region* home;
BWTA::Region* enemy_base;

Unit* chokeGuardian = NULL; 
BWTA::Chokepoint* choke=NULL;


// choke analysis
std::string clingoProgramText;  
std::vector<std::pair<int, int> > buildTiles;
std::vector<std::pair<int, int> > supplyTiles;
std::vector<std::pair<int, int> > barracksTiles;
std::vector<std::pair<int, int> > walkableTiles;
std::vector<std::pair<int, int> > outsideTiles;	  
std::pair<int, int> insideBase, outsideBase;

std::vector<std::pair<UnitType, TilePosition> > wallLayout;
bool wallData = true;
std::vector<std::pair<BWAPI::UnitType, BWAPI::TilePosition> > ITUBot::_wall;


// Drawing variables  (to be changed later)
UnitType drawWhat;
TilePosition drawPos;
bool draw = false;

// Build Order building variables
Unit* builder = NULL;
bool FoWError = false;	// fog of war error
int bLastChecked = 0;	// building last checked at frame:

// constants
const int BTSize = 32;	// build tile size
const int WTSize = 8;	// walk tile size

//////////////////// END OF GLOBAL VARIABLES

// function prototypes
void guardChoke(Unit*);
void back2work(Unit*);
TilePosition getBuildTile(Unit* u, UnitType b, Position p, bool shrink = false);
Unit* pickBuilder();
std::pair<int, int> findClosestTile(const std::vector<std::pair<int, int> >& tiles);
std::pair<int, int> findFarthestTile(const std::vector<std::pair<int, int> >& tiles);

void ITUBot::onStart(){
	Broodwar->sendText("ITUBot says Hello world!");
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
		}	
	

		// populate build order - hardcoded
		populateBuildOrder();

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

	if (analyzed){
		drawTerrainData();
		drawChokeData();
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

		else executeBuildOrder(*u);	//execute order 66 lol

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
	} else if (text=="/asp")
	{
		wallData = !wallData;
	} else if (text=="/opt" && analyzed == false)
	{
		optimizeGap = !optimizeGap;
		optimizeGap ? BWAPI::Broodwar->printf("Optimization: GAP") 
					: BWAPI::Broodwar->printf("Optimization: COST");
	}
	
	else
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
  //if (!Broodwar->isReplay() && Broodwar->getFrameCount()>1);
    //Broodwar->sendText("A %s [%x] has been discovered at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
}

void ITUBot::onUnitEvade(BWAPI::Unit* unit)
{
  if (!Broodwar->isReplay() && Broodwar->getFrameCount()>1)
    ;//Broodwar->sendText("A %s [%x] was last accessible at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
}

void ITUBot::onUnitShow(BWAPI::Unit* unit)
{
  //if (!Broodwar->isReplay() && Broodwar->getFrameCount()>1);
    //Broodwar->sendText("A %s [%x] has been spotted at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
}

void ITUBot::onUnitHide(BWAPI::Unit* unit)
{
  if (!Broodwar->isReplay() && Broodwar->getFrameCount()>1)
   ;// Broodwar->sendText("A %s [%x] was last seen at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
}

void ITUBot::onUnitCreate(BWAPI::Unit* unit)
{
	if (Broodwar->getFrameCount()>1)
	{
		if (!Broodwar->isReplay()){
			//Broodwar->sendText("A %s [%x] has been created at (%d,%d)=(%d,%d)",unit->getType().getName().c_str(),unit,
			//																	unit->getPosition().x(),unit->getPosition().y(),
			//																	unit->getPosition().x()/32,unit->getPosition().y()/32);
			if(_buildOrder.empty() == false && unit->getType() == _buildOrder.front()){
				if( unit->getType().isBuilding() ){
					builder = NULL;
					Broodwar->printf("%s completed. Build order remaining size: %d", _buildOrder.front().getName().c_str(), _buildOrder.size()-1);
					_buildOrder.pop();
					// find in _wall
					bool found = false;
					for(unsigned i=0; i< ITUBot::_wall.size() ; ++i){
						if( ITUBot::_wall[i].first == unit->getType() ){
							ITUBot::_wall.erase( ITUBot::_wall.begin() + i );
							break;
						}
					}
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
	if (!Broodwar->isReplay()){
		Broodwar->sendText("A %s [%x] has been morphed at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
		if(_buildOrder.empty() == false && unit->getType() == _buildOrder.front()){
			if( unit->getType().isBuilding() ){
				builder = NULL;
				Broodwar->printf("%s completed. Build order remaining size: %d", _buildOrder.front().getName().c_str(), _buildOrder.size()-1);
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
			Broodwar->sendText("%.2d:%.2d: %s morphs a %s",minutes,seconds,unit->getPlayer()->getName().c_str(),unit->getType().getName().c_str());

			
		}

		
	}
}

void ITUBot::onUnitComplete(BWAPI::Unit *unit){
	if ( !Broodwar->isReplay() && Broodwar->getFrameCount() > 1 ){
		/*Broodwar->sendText("A %s [%x] has been completed at (%d,%d)",
							unit->getType().getName().c_str(),
							unit,unit->getPosition().x(),
							unit->getPosition().y()
							);
		 */
		// send troops to choke point
		if(unit->getType().isWorker() == false && unit->getType().canAttack() == true){
			if( choke != NULL){
				unit->rightClick(choke->getCenter());
			}
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

//////
void analyzeChoke(){

 	// choke point analysis
	int x =	choke->getCenter().x(); int y = choke->getCenter().y();
	int maxDist = 10;				
	int tileX = x/BTSize;			int tileY = y/BTSize;

	// analysis of build tiles near choke points
	for (int i = tileX - maxDist ; i <= tileX + maxDist ; ++i){
		for(int j = tileY - maxDist ; j <= tileY + maxDist ; ++j){

			// if the tile is in home region
			if( BWTA::getRegion(TilePosition(i,j)) == home){
				// and it is buildable, add it to the buildTiles
				if( Broodwar->isBuildable( TilePosition(i,j) ) ){
					buildTiles.push_back( std::make_pair(i, j) );
				}
			}
			// if the tile is outside the home region
			else {
				// and it is walkable, add it to the outside tiles
				if( Broodwar->isWalkable(i*BTSize/WTSize +1, j*BTSize/WTSize +2) &&
					Broodwar->isWalkable(i*BTSize/WTSize +1, j*BTSize/WTSize +1) &&
					Broodwar->isWalkable(i*BTSize/WTSize +2, j*BTSize/WTSize +1) &&
					Broodwar->isWalkable(i*BTSize/WTSize +2, j*BTSize/WTSize +2) 
					)
					outsideTiles.push_back( std::make_pair(i, j) );
			}
			
			  
		}  
	}

	maxDist += 4;
	for (int i = tileX - maxDist ; i <= tileX + maxDist ; ++i){
		for(int j = tileY - maxDist ; j <= tileY + maxDist ; ++j){
			if( Broodwar->isWalkable(i*BTSize/WTSize +1, j*BTSize/WTSize +2) &&
				Broodwar->isWalkable(i*BTSize/WTSize +1, j*BTSize/WTSize +1) &&
				Broodwar->isWalkable(i*BTSize/WTSize +2, j*BTSize/WTSize +1) &&
				Broodwar->isWalkable(i*BTSize/WTSize +2, j*BTSize/WTSize +2) 
				)
				walkableTiles.push_back( std::make_pair(i, j) );
		}
	}


	Unit* builderr = NULL;
	builderr = pickBuilder();

	assert(builderr != NULL);

	// analyze buildable tiles for buildings that we will use to wall in (barracks and supply depot)
	for(unsigned i=0; i<buildTiles.size() ; ++i){
		TilePosition pos( buildTiles[i].first, buildTiles[i].second );

		if( Broodwar->canBuildHere( builderr, pos, UnitTypes::Terran_Supply_Depot, false ) )
			supplyTiles.push_back( std::make_pair(pos.x(), pos.y()) );

		if( Broodwar->canBuildHere( builderr, pos, UnitTypes::Terran_Barracks, false ) )
			barracksTiles.push_back( std::make_pair(pos.x(), pos.y()) );
	}
	
	
	return;
}

DWORD WINAPI AnalyzeThread(){
	BWTA::analyze();

	// self start location only available if the map has base locations
	if (BWTA::getStartLocation(BWAPI::Broodwar->self())!=NULL)
		home       = BWTA::getStartLocation(BWAPI::Broodwar->self())->getRegion();
	
	// enemy start location only available if Complete Map Information is enabled.
	if (BWTA::getStartLocation(BWAPI::Broodwar->enemy())!=NULL)
		enemy_base = BWTA::getStartLocation(BWAPI::Broodwar->enemy())->getRegion();
	
	// assign the closest choke point
	std::set<BWTA::Chokepoint*> chokepoints= home->getChokepoints();
	double min_length=10000;

	// iterate through all chokepoints and look for the one with the smallest gap (least width)
	for(std::set<BWTA::Chokepoint*>::iterator c=chokepoints.begin();c!=chokepoints.end();c++){
		double length=(*c)->getWidth();
		if (length<min_length || choke==NULL){
			min_length=length;
			choke=*c;
		}
	}

	analyzeChoke();	   
	ITUBot::initClingoProgramSource();
	ITUBot::runASPSolver();
	
	analyzed   = true;
	analysis_just_finished = true;
	BWAPI::Broodwar->printf("Finished analyzing map.");
	return 0;
}

/////
void ITUBot::drawStats()
{
	// Display the game frame rate as text in the upper left area of the screen
	Broodwar->drawTextScreen(200, 0,  "FPS: %d", Broodwar->getFPS() );
	Broodwar->drawTextScreen(200, 20, "Average FPS: %f", Broodwar->getAverageFPS() );

	std::set<Unit*> myUnits = Broodwar->self()->getUnits();
	Broodwar->drawTextScreen(5,0,"I have %d units:",myUnits.size());
	std::map<UnitType, int> unitTypeCounts;
	for(std::set<Unit*>::iterator i=myUnits.begin();i!=myUnits.end();i++){
		if (unitTypeCounts.find((*i)->getType())==unitTypeCounts.end()){
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
	optimizeGap ? Broodwar->drawTextScreen(5, 16*line, "Optimization Mode: Gap") 
				: Broodwar->drawTextScreen(5, 16*line, "Optimization Mode: Cost");	

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

void ITUBot::drawChokeData(){
	
	if(!wallData){
		// draw walkable tiles
		for(unsigned i = 0; i < walkableTiles.size() ; ++i){
			int x = walkableTiles[i].first;
			int y = walkableTiles[i].second;
			Broodwar->drawBoxMap(x*BTSize , y*BTSize, (x+1)*BTSize, (y+1)*BTSize, Colors::Purple, false);
			Broodwar->drawTextMap(x*BTSize, y*BTSize, "%d,\n%d", x, y);
		}

		// draw inside buildable tiles
		for(unsigned i = 0; i < buildTiles.size() ; ++i){
			int x = buildTiles[i].first;
			int y = buildTiles[i].second;
			Broodwar->drawBoxMap(x*BTSize+3, y*BTSize+3, (x+1)*BTSize-3, (y+1)*BTSize-3, Colors::Green, false);
			Broodwar->drawTextMap(x*BTSize, y*BTSize, "%d,\n%d", x, y);
		}
		
		 // draw possible supply depot tiles
		for(unsigned i = 0; i < supplyTiles.size() ; ++i){
			int x = supplyTiles[i].first;
			int y = supplyTiles[i].second;
			Broodwar->drawBoxMap(x*BTSize+5 , y*BTSize+5, (x+1)*BTSize-5, (y+1)*BTSize-5, Colors::Red, false);
		}
		
		// draw possible barracks tiles
		for(unsigned i = 0; i < barracksTiles.size() ; ++i){
			int x = barracksTiles[i].first;
			int y = barracksTiles[i].second;
			Broodwar->drawBoxMap(x*BTSize+8 , y*BTSize+8, (x+1)*BTSize-8, (y+1)*BTSize-8, Colors::Blue, false);
		}

		// draw outside buildable tiles 
		for(unsigned i = 0; i < outsideTiles.size() ; ++i){
			int x = outsideTiles[i].first;
			int y = outsideTiles[i].second;
			Broodwar->drawBoxMap(x*BTSize+3 , y*BTSize+3, (x+1)*BTSize-3, (y+1)*BTSize-3, Colors::Cyan, false);
			Broodwar->drawTextMap(x*BTSize, y*BTSize, "%d,\n%d", x, y);
		}

		
		int x,y;

		x = insideBase.first; y = insideBase.second;
		Broodwar->drawBoxMap(x*BTSize , y*BTSize, (x+1)*BTSize, (y+1)*BTSize, Colors::Green, true);

		x = outsideBase.first; y = outsideBase.second;
		Broodwar->drawBoxMap(x*BTSize , y*BTSize, (x+1)*BTSize, (y+1)*BTSize, Colors::Cyan, true);

		/*
		// walk tile analysis
		tileX = x/WTSize;	tileY = y/WTSize;	maxDist *= BTSize/WTSize;
		for (int i = tileX - maxDist ; i <= tileX + maxDist ; ++i){
			for(int j = tileY - maxDist ; j <= tileY + maxDist ; ++j){
				
				if( Broodwar->isWalkable( i,j ) ){
					//Broodwar->drawBoxMap(i*WTSize, j*WTSize, WTSize*(i+1), WTSize*(j+1), Colors::Blue, false);
					//Broodwar->drawTextMap(i*WTSize, j*WTSize, "\x11%d, %d", i, j);  
					Broodwar->drawCircleMap(i*WTSize, j*WTSize, 1, Colors::Orange, false);
				}

				
				  
			}  
		}
		*/
	}
	else{	   // if wall data
		for(unsigned i = 0; i < wallLayout.size() ; ++i){
			int x = wallLayout[i].second.x();
			int y = wallLayout[i].second.y();
			int h = wallLayout[i].first.tileHeight();
			int w = wallLayout[i].first.tileWidth();
			Broodwar->drawBoxMap(x*BTSize, y*BTSize, (x+w)*BTSize, (y+h)*BTSize, Colors::Orange, false);
		}
	}


	// draw region centers
	std::pair<BWTA::Region*, BWTA::Region*> regs = choke->getRegions();
	Broodwar->drawTextMap(regs.first->getCenter().x(),  regs.first->getCenter().y(), "Center 1");
	Broodwar->drawTextMap(regs.second->getCenter().x(), regs.second->getCenter().y(), "Center 2"); 
}

/////
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


/////
void ITUBot::populateBuildOrder(){
	_buildOrder.push(UnitTypes::Terran_SCV);			// 5
	_buildOrder.push(UnitTypes::Terran_SCV);			// 6
	_buildOrder.push(UnitTypes::Terran_SCV);			// 7
	_buildOrder.push(UnitTypes::Terran_SCV);			// 8
	_buildOrder.push(UnitTypes::Terran_Barracks);
	_buildOrder.push(UnitTypes::Terran_SCV);			// 9
	_buildOrder.push(UnitTypes::Terran_Supply_Depot);
	_buildOrder.push(UnitTypes::Terran_SCV);			// 10
	_buildOrder.push(UnitTypes::Terran_SCV);
	_buildOrder.push(UnitTypes::Terran_Supply_Depot);
	_buildOrder.push(UnitTypes::Terran_SCV);
	_buildOrder.push(UnitTypes::Terran_Marine);
	_buildOrder.push(UnitTypes::Terran_SCV);	  
	_buildOrder.push(UnitTypes::Terran_Marine);	 
	_buildOrder.push(UnitTypes::Terran_Marine);
	_buildOrder.push(UnitTypes::Terran_Supply_Depot);
	_buildOrder.push(UnitTypes::Terran_Refinery);

	Broodwar->printf("Build order populated. Size = %d", _buildOrder.size());
}
void ITUBot::executeBuildOrder(Unit* unit){

	UnitType toBuild = buildOrder().front();
	if(toBuild.isBuilding() == true){

		// if a worker is free				&&  
		// if we have sufficient minerals	&&  
		// if the map analysis is done
		if(unit->getType().isWorker() && !unit->isConstructing() && unit->isCarryingMinerals() &&
			Broodwar->self()->minerals() >= toBuild.mineralPrice() &&
			home != NULL && analyzed == true
			)
		{

			// find a suitable location if the builder is not assigned to work yet		
			TilePosition targetBuildLocation(-1, -1);	// invalid location by default

			if(builder == NULL || FoWError == true ){
				
				// find in _wall
				bool found = false;
				for(unsigned i=0; i< ITUBot::_wall.size() ; ++i){
					if( ITUBot::_wall[i].first == toBuild ){
						found = true;
						targetBuildLocation = ITUBot::_wall[i].second;
						break;
					}
				}

				if(found == false)
					targetBuildLocation = getBuildTile(unit, toBuild, home->getCenter());
				bLastChecked = Broodwar->getFrameCount();
			}

			// if location is found but the building is not being constructed for some reason
			else{
				// if the reason is known by BWAPI
				if(	Broodwar->getLastError() != Errors::None )
					Broodwar->printf("Last Error: %s", Broodwar->getLastError().c_str());
				else{
					if( bLastChecked + 24*10 < Broodwar->getFrameCount()){
						Broodwar->printf("Assigning worker again.");
						builder = NULL;
						bLastChecked = Broodwar->getFrameCount();
					}
				}
			}	
			if ( targetBuildLocation.x() != -1 && targetBuildLocation.y() != -1 ){

				// draw the layout
				draw = true;
				drawPos = targetBuildLocation;
				drawWhat = toBuild;
				
				// Order the builder to construct the barracks
				if (builder == NULL || FoWError == true){
					if (builder == NULL) builder = unit;
					if ( builder->build( targetBuildLocation, toBuild ) == false &&
							builder->isIdle() == false // exclude builder being busy for error printing
						){
						
						if( Broodwar->getLastError() != Errors::None && Broodwar->getLastError() != Errors::Unbuildable_Location)
							Broodwar->printf("%s build (%d, %d) failed due to %s", toBuild.getName().c_str(), 
																					targetBuildLocation.x(), 
																					targetBuildLocation.y(),
																					Broodwar->getLastError().c_str());
						else if( FoWError == false ){
							Broodwar->printf("%s build (%d, %d) failed due to Fog of War.", toBuild.getName().c_str(), 
																							targetBuildLocation.x(), 
																							targetBuildLocation.y());
							builder->rightClick((BWAPI::Position)targetBuildLocation);
							Broodwar->printf("A worker is sent to the position (%d, %d)",   targetBuildLocation.x(), 
																							targetBuildLocation.y());
							FoWError = true;
						}
					}	
					else{
						FoWError = false;
					}
				}
		
			} // closure: if valid location	
		}  // closure: if enough resources

	}   // closure: building

	// if we are training units
	else{

		// if the unit is the building that trains the unit in the build order		&&
		// if the structure that is goint to train the unit is completed
		// if we have enough supply to build the unit								&&
		// if we have the sufficient resources
		if( unit->getType() == toBuild.whatBuilds().first && 
			unit->isCompleted() &&
			Broodwar->self()->supplyUsed() <= Broodwar->self()->supplyTotal() - toBuild.supplyRequired() &&
			Broodwar->self()->minerals() >= toBuild.mineralPrice()
			)
		{
			static int lastChecked = 0;

			// wait a little (1 sec) before training for resources to adjust
			if( lastChecked + 24 < Broodwar->getFrameCount() ){			

				if ( unit->train(toBuild) == true ){
					Broodwar->printf("%s completed. Build order remaining size: %d", toBuild.getName().c_str(), _buildOrder.size()-1);
					buildOrder().pop();
					lastChecked = Broodwar->getFrameCount();
				}
				else{
					Broodwar->printf("%s - %s train failed.", Broodwar->getLastError().c_str(), toBuild.getName().c_str());
				}
			}
		}
	}  // closure: unit

}

void ITUBot::initClingoProgramSource(){
	std::ofstream file;

	file.open("bwapi-data/AI/ITUBotWall.txt");
	if(file.is_open()){

		file << "% Building / Unit types" << endl
			<< "buildingType(marineType).	" << endl
			<< "buildingType(barracksType)." << endl
			<< "buildingType(supplyDepotType).	" << endl  << endl

			<< "% Size specifications" << endl
			<< "width(marineType,1).	height(marineType,1)." << endl
			<< "width(barracksType,4).	height(barracksType,3)." << endl
			<< "width(supplyDepotType,3). 	height(supplyDepotType,2)." << endl	 << endl

			<< "costs(supplyDepotType, 100)." << endl
			<< "costs(barracksType, 150)." << endl

			<< "% Gaps" << endl
			<< "leftGap(barracksType,16). 	rightGap(barracksType,7).	topGap(barracksType,8). 	bottomGap(barracksType,15)." << endl
			<< "leftGap(marineType,0). 		rightGap(marineType,0). 	topGap(marineType,0). 		bottomGap(marineType,0)." << endl
			<< "leftGap(supplyDepotType,10).		 rightGap(supplyDepotType,9). 	topGap(supplyDepotType,10). 		bottomGap(supplyDepotType,5)." << endl	 << endl

			<< "% Facts" << endl
			<< "building(marine1).	type(marine1, marineType)." << endl
			<< "building(barracks1).	type(barracks1, barracksType)." << endl		
			<< "building(barracks2).	type(barracks2, barracksType)." << endl
			<< "building(supplyDepot1).	type(supplyDepot1, supplyDepotType)." << endl
			<< "building(supplyDepot2).	type(supplyDepot2, supplyDepotType)." << endl 
			<< "building(supplyDepot4).	type(supplyDepot4, supplyDepotType)." << endl   
			<< "building(supplyDepot3).	type(supplyDepot3, supplyDepotType)." << endl << endl
								  
			<< "% Constraint: two units/buildings cannot occupy the same tile" << endl
			<< ":- occupiedBy(B1, X, Y), occupiedBy(B2, X, Y), B1 != B2." << endl	  << endl
				 
			<< "% Tiles occupied by buildings" << endl
			<< "occupiedBy(B,X2,Y2) :- place(B, X1, Y1)," << endl
			<< "						type(B, BT), width(BT,Z), height(BT, Q)," << endl
			<< "						X2 >= X1, X2 < X1+Z, Y2 >= Y1, Y2 < Y1+Q," << endl
			<< "						walkableTile(X2, Y2)." << endl
			<< "						" << endl  << endl

			<< "% Gaps between two adjacent tiles, occupied by buildings." << endl
			<< "verticalGap(X1,Y1,X2,Y2,G) :-" << endl
			<< "	occupiedBy(B1,X1,Y1), occupiedBy(B2,X2,Y2)," << endl
			<< "	B1 != B2, X1=X2, Y1=Y2-1, G=S1+S2," << endl
			<< "	type(B1,T1), type(B2,T2), bottomGap(T1,S1), topGap(T2,S2)." << endl 
			<< "	" << endl
			<< "verticalGap(X1,Y1,X2,Y2,G) :-" << endl
			<< "	occupiedBy(B1,X1,Y1), occupiedBy(B2,X2,Y2)," << endl
			<< "	B1 != B2, X1=X2, Y1=Y2+1, G=S1+S2," << endl
			<< "	type(B1,T1), type(B2,T2), bottomGap(T2,S2), topGap(T1,S1)." << endl
			<< "	" << endl
			<< "horizontalGap(X1,Y1,X2,Y2,G) :-" << endl
			<< "	occupiedBy(B1,X1,Y1), occupiedBy(B2,X2,Y2)," << endl
			<< "	B1 != B2, X1=X2-1, Y1=Y2, G=S1+S2," << endl
			<< "	type(B1,T1), type(B2,T2), rightGap(T1,S1), leftGap(T2,S2)." << endl		 << endl

			<< "horizontalGap(X1,Y1,X2,Y2,G) :-" << endl
			<< "	occupiedBy(B1,X1,Y1), occupiedBy(B2,X2,Y2)," << endl
			<< "	B1 != B2, X1=X2+1, Y1=Y2, G=S1+S2," << endl
			<< "	type(B1,T1), type(B2,T2), rightGap(T2,S2), leftGap(T1,S1)." << endl<< endl

			<< "cost(B, C) :- place(B, X, Y), type(B, BT), costs(BT, COST), C=COST." << endl

			///////////////
			<< "% Tile information" << endl;

			for(std::vector<std::pair<int, int> >::const_iterator it = walkableTiles.begin();
				it != walkableTiles.end(); ++it){
				file << "walkableTile(" << it->first << ", " << it->second << ")." << endl;
			}

			for(std::vector<std::pair<int, int> >::const_iterator it = barracksTiles.begin();
				it != barracksTiles.end(); ++it){
				file << "buildable(barracksType, " << it->first << ", " << it->second << ")." << endl;
			}

			for(std::vector<std::pair<int, int> >::const_iterator it = supplyTiles.begin();
				it != supplyTiles.end(); ++it){
				file << "buildable(supplyDepotType, " << it->first << ", " << it->second << ")." << endl;
			}
			////////////////////////

			insideBase = findClosestTile(buildTiles);
			outsideBase = findFarthestTile(outsideTiles);
			file << endl << "insideBase(" << insideBase.first << ", " << insideBase.second << ").\t";
			file << "outsideBase(" << outsideBase.first << ", " << outsideBase.second << ")." << endl << endl


			<< "% Constraint: Inside of the base must not be reachable." << endl
			<< ":- insideBase(X2,Y2), outsideBase(X1,Y1), canReach(X2,Y2)." << endl	<< endl

			<< "% Reachability between tiles." << endl
			<< "blocked(X,Y) :- occupiedBy(B,X,Y), building(B), walkableTile(X,Y)." << endl
			<< "canReach(X,Y) :- outsideBase(X,Y)." << endl	 << endl

			<< "canReach(X2,Y) :-" << endl
			<< "	canReach(X1,Y), X1=X2+1, walkableTile(X1,Y), walkableTile(X2,Y)," << endl
			<< "	not blocked(X1,Y), not blocked(X2,Y)." << endl
			<< "canReach(X2,Y) :-" << endl
			<< "	canReach(X1,Y), X1=X2-1, walkableTile(X1,Y), walkableTile(X2,Y)," << endl
			<< "	not blocked(X1,Y), not blocked(X2,Y)." << endl
			<< "canReach(X,Y2) :-" << endl
			<< "	canReach(X,Y1), Y1=Y2+1, walkableTile(X,Y1), walkableTile(X,Y2)," << endl
			<< "	not blocked(X,Y1), not blocked(X,Y2)." << endl
			<< "canReach(X,Y2) :-" << endl
			<< "	canReach(X,Y1), Y1=Y2-1, walkableTile(X,Y1), walkableTile(X,Y2)," << endl
			<< "	not blocked(X,Y1), not blocked(X,Y2)." << endl
			<< "canReach(X2,Y2) :-" << endl
			<< "	canReach(X1,Y1), X1=X2+1, Y1=Y2+1, walkableTile(X1,Y1), walkableTile(X2,Y2)," << endl
			<< "	not blocked(X1,Y1), not blocked(X2,Y2)." << endl
			<< "canReach(X2,Y2) :-" << endl
			<< "	canReach(X1,Y1), X1=X2-1, Y1=Y2+1, walkableTile(X1,Y1), walkableTile(X2,Y2)," << endl
			<< "	not blocked(X1,Y1), not blocked(X2,Y2)." << endl
			<< "canReach(X2,Y2) :-" << endl
			<< "	canReach(X1,Y1), X1=X2+1, Y1=Y2-1, walkableTile(X1,Y1), walkableTile(X2,Y2)," << endl
			<< "	not blocked(X1,Y1), not blocked(X2,Y2)." << endl
			<< "canReach(X2,Y2) :-" << endl
			<< "	canReach(X1,Y1), X1=X2-1, Y1=Y2-1, walkableTile(X1,Y1), walkableTile(X2,Y2)," << endl
			<< "	not blocked(X1,Y1), not blocked(X2,Y2)." << endl	   << endl

			<< "% Using gaps to reach (walk on) blocked locations." << endl
			<< "enemyUnitX(32). enemyUnitY(32)." << endl
			<< "canReach(X1,Y1) :- horizontalGap(X1,Y1,X2,Y1,G), G >= S, X2=X1+1, canReach(X1,Y3), Y3=Y1+1, enemyUnitX(S)." << endl
			<< "canReach(X1,Y1) :- horizontalGap(X1,Y1,X2,Y1,G), G >= S, X2=X1-1, canReach(X1,Y3), Y3=Y1+1, enemyUnitX(S)." << endl
			<< "canReach(X1,Y1) :- horizontalGap(X1,Y1,X2,Y1,G), G >= S, X2=X1+1, canReach(X1,Y3), Y3=Y1-1, enemyUnitX(S)." << endl
			<< "canReach(X1,Y1) :- horizontalGap(X1,Y1,X2,Y1,G), G >= S, X2=X1-1, canReach(X1,Y3), Y3=Y1-1, enemyUnitX(S)." << endl
			<< "canReach(X1,Y1) :- verticalGap(X1,Y1,X1,Y2,G), G >= S, Y2=Y1+1, canReach(X3,Y1), X3=X1-1, enemyUnitY(S)." << endl
			<< "canReach(X1,Y1) :- verticalGap(X1,Y1,X1,Y2,G), G >= S, Y2=Y1-1, canReach(X3,Y1), X3=X1-1, enemyUnitY(S)." << endl
			<< "canReach(X1,Y1) :- verticalGap(X1,Y1,X1,Y2,G), G >= S, Y2=Y1+1, canReach(X3,Y1), X3=X1+1, enemyUnitY(S)." << endl
			<< "canReach(X1,Y1) :- verticalGap(X1,Y1,X1,Y2,G), G >= S, Y2=Y1-1, canReach(X3,Y1), X3=X1+1, enemyUnitY(S)." << endl

			//<< ":- place(supplyDepot2, X, Y) | place(barracks2, X, Y)." << endl			// error: |
			//<< ":- not place(supplyDepot2, X, Y), not place(barracks2, X, Y)." << endl	// unsafe variables X & Y

			<< "% Generate all the potential placements." << endl
			<< "1[place(barracks1,X,Y) : buildable(barracksType,X,Y)]1." << endl 
			<< "0[place(barracks2,X,Y) : buildable(barracksType,X,Y)]1." << endl 

			<< "1[place(supplyDepot1,X,Y) : buildable(supplyDepotType,X,Y)]1." << endl
			<< "0[place(supplyDepot2,X,Y) : buildable(supplyDepotType,X,Y)]1." << endl
			<< "0[place(supplyDepot3,X,Y) : buildable(supplyDepotType,X,Y)]1." << endl	    
			<< "0[place(supplyDepot4,X,Y) : buildable(supplyDepotType,X,Y)]1." << endl << endl
																  
			<< "% Optimization criterion" << endl;	

			if(optimizeGap){
				file << "#minimize [horizontalGap(X1,Y1,X2,Y2,G) = G @1 ]." << endl	
				<< "#minimize [verticalGap(X1,Y1,X2,Y2,G) = G @1 ]." << endl
				<< "#minimize [place(supplyDepot2,X,Y) @2]." << endl       
				<< "#minimize [place(supplyDepot3,X,Y) @2]." << endl	
				<< "#minimize [place(supplyDepot4,X,Y) @2]." << endl	
				<< "#minimize [place(barracks2,X,Y) @2]. " << endl	 << endl;	 
			}
			else
				file << "#minimize [cost(B, C) = C]." << endl;


			file << "#hide." << endl
			<< "#show place/3." << endl;



		BWAPI::Broodwar->printf("ASP Solver Contents Ready!");

		optimizeGap ? BWAPI::Broodwar->printf("Optimization: GAP") 
					: BWAPI::Broodwar->printf("Optimization: COST");


		file.close();
	}
	else											  
		BWAPI::Broodwar->printf("Error Opening File");

	

}
void ITUBot::runASPSolver(){							 

	// relative path doesn't work. Don't know why..
	system("D:/SCAI/IT_WORKS/BWAPI/ITUBot/Clingo/clingo.exe D:/SCAI/IT_WORKS/StarCraft/bwapi-data/AI/ITUBotWall.txt > D:/SCAI/IT_WORKS/StarCraft/bwapi-data/AI/out.txt");
	//system("../BWAPI/ITUBot/Clingo/clingo.exe bwapi-data/AI/ITUBotWall.txt > bwapi-data/AI/solver-out.txt");
	
	std::vector<std::string> lines;
	std::string line;
	unsigned lineCounter = 0;
	std::ifstream file("D:/SCAI/IT_WORKS/StarCraft/bwapi-data/AI/out.txt");
	if(file.is_open()){
        while( getline(file, line) ){
            if(*(line.end()-1) == '\r')
                line.erase(line.end()-1);
            lines.push_back(line);
            if(line == "OPTIMUM FOUND"){
                line = lines[lineCounter-2];	// contains final answer that will be parsed
                break;
            }
			if(line == "UNSATISFIABLE"){	   // error in solver
				Broodwar->printf("Solver failed finding a solution!");
				return;
			}
            lineCounter++;
        }

		// parse the answer, example output below
		// place(supplyDepot1,119,46) place(supplyDepot2,122,44) place(barracks1,116,52) 
		std::stringstream ss;
		std::string token;
        while(line != ""){	 // tokenizin the whole line
			std::vector<int> coords;
			UnitType type;
			int val;
			

            ss << line.substr(6, line.find(")") - 6);   // place( = 6 chars
            while(getline(ss, token, ',')){	   // tokenizing the individual place() statements
				std::istringstream iss(token);
				iss >> val;

				if( iss.fail() ){
					std::size_t found = token.find("supplyDepot");	// search for supply depot

					// if found its supply depot, if not found its barracks (early wall)
					type =	found!=std::string::npos ? UnitTypes::Terran_Supply_Depot : UnitTypes::Terran_Barracks;
				}
				else{   // coordinates
					coords.push_back(val);
				}
            }

			// erase the parsed part including the closing parenthesis and space
            line.erase(0, line.find(")")+2);
            
			// clear buffers
			ss.clear();
            token.clear();

			// add the result to data structure after successful parsing for each place() statement
			wallLayout.push_back( std::make_pair(type, TilePosition(coords[0], coords[1])) );
        }

		// save results into bots memory
		ITUBot::_wall = wallLayout;
		
        file.close();

		// finally, add choke point width to the output file
		std::ofstream  oFile("D:/SCAI/IT_WORKS/StarCraft/bwapi-data/AI/out.txt", std::ios::app);

		if(oFile.is_open()){
			oFile << "Choke Width: " << choke->getWidth() << endl;
			oFile.close();
		}
		else{
			Broodwar->printf("Error opening output file");
		}
    }
	else
		Broodwar->printf("** ERROR OPENING SOLVER OUTPUT FILE");
	

}											
////////////////////////////////////

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



TilePosition getBuildTile(Unit* u, UnitType b, Position p, bool shrink){
	int x =	p.x(); int y = p.y();
	TilePosition ret(-1, -1);
	int maxDist = 3;
	int stopDist = 30;
	int tileX = x/32; int tileY = y/32;

	if(shrink == true)	stopDist -= 5;

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
							//Broodwar->printf("   X: %d, Y:%d for position (%d, %d)", ret.x(), ret.y(), p.x(), p.y());
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


Unit* pickBuilder(){

	// Iterate through all the units that we own
	const std::set<Unit*> myUnits = Broodwar->self()->getUnits();
	for ( std::set<Unit*>::const_iterator un = myUnits.begin(); un != myUnits.end(); ++un ){
		if( (*un)->getType().isWorker() && (*un)->isConstructing() == false) {

			//BWAPI::Broodwar->printf("found one" );
			return *un;
			
		}
	}

	//BWAPI::Broodwar->printf("Worker Not Found, so, NULL");
	return NULL;
}	 


std::pair<int, int> findClosestTile(const std::vector<std::pair<int, int> >& tiles){
	std::pair<int, int> ret;
	Position p;
	std::set<Unit*> units = Broodwar->self()->getUnits();
	for(std::set<Unit*>::const_iterator u = units.begin() ; u != units.end() ; ++u){
		if( (*u)->getType() == UnitTypes::Terran_Command_Center ){
			p = (*u)->getPosition();
			break;
		}
	}
	
	double dist = 9000000000;

	for(std::vector<std::pair<int, int> >::const_iterator it = walkableTiles.begin() ; it != walkableTiles.end() ; ++it){
		if(p.getDistance( Position(it->first*BTSize, it->second*BTSize) ) <= dist && 
			p.hasPath( Position(it->first*BTSize, it->second*BTSize) ) 
		){
			dist =	p.getDistance( Position(it->first*BTSize, it->second*BTSize) );
			ret = *it;
		}
	}   

	//ret.first = p.x()/BTSize; 
	//ret.second = p.y()/BTSize;
	return ret;
}

std::pair<int, int> findFarthestTile(const std::vector<std::pair<int, int> >& tiles){
	std::pair<int, int> ret;

	// get the position of the command center
	Position p;
	std::set<Unit*> units = Broodwar->self()->getUnits();
	for(std::set<Unit*>::const_iterator u = units.begin() ; u != units.end() ; ++u){
		if( (*u)->getType() == UnitTypes::Terran_Command_Center ){
			p = (*u)->getPosition();
			break;
		}
	}
	
	double dist = 0;

	// pick the farthest tile
	for(std::vector<std::pair<int, int> >::const_iterator it = tiles.begin() ; it != tiles.end() ; ++it){
		if(p.getDistance( Position(it->first*BTSize, it->second*BTSize) ) >= dist && 
			p.hasPath( Position(it->first*BTSize, it->second*BTSize) ) 
		){
			dist =	p.getDistance( Position(it->first*BTSize, it->second*BTSize) );
			ret = *it;
		}
	}

	return ret;
}
