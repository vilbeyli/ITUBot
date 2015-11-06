ITUBot
======

ITUBot is a StarCraft Broodwar AI Bot that implements and modifies [Certicky's wall-in algorithm](http://arxiv.org/abs/1306.4460) created using logic programming.

The problem is formulated as a [constraint satisfaction problem](http://en.wikipedia.org/wiki/Constraint_satisfaction_problem) and solved by the Potsdam University's [ASP (Answer Set Programming)](http://en.wikipedia.org/wiki/Answer_set_programming) solver: [*clasp*](http://www.cs.uni-potsdam.de/clasp/). The solver is tested with two optimization modes: gap minimization and resource cost minimization. A [**detailed report***](https://www.dropbox.com/s/ru5tmhz4vh6eihh/Walling%20in%20at%20StarCraft.pdf?dl=0) about the analysis is generated. The report contains:
 - Basic bot architecture
 - A step by step explanation of Certicky's modified algorithm
 - Algorithm run times for each optimization criteria on several different maps
 - Differences in the layout of the buildings in the solution between different optimization criteria
 - Related studies and future work


[Here is a video of ITUBot walling in.](http://www.youtube.com/watch?v=WdhIv_yxIbM)

*: <sub>Sorry for the horrible formatting, the university formatting rules were unfortunately strict.</sub>

-------------

The project is coded on Visual Studio 2008 Express. 

**Update**: Oh god, its horrible to look at that code. Apologies for the coding horrors in the base code, it was my first real-time program. Hopefully the report will help make more sense.

Related Events/Groups
=======

 - [Student StarCraft AI Tournament (SSCAI)](http://www.sscaitournament.com/)
 - [AIIDE StarCraft AI Competition](http://webdocs.cs.ualberta.ca/~cdavid/starcraftaicomp/index.shtml)
 - [BWAPI Facebook Group](https://www.facebook.com/groups/bwapi/)
 - [StarCraft AI Wiki](http://www.starcraftai.com/wiki/Main_Page)
