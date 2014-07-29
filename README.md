ITUBot
======

ITUBot is a StarCraft Broodwar AI Bot that implements and modifies [Certicky's wall-in algorithm](http://arxiv.org/abs/1306.4460) created using logic programming.

The problem is formulated as a [constraint satisfaction problem](http://en.wikipedia.org/wiki/Constraint_satisfaction_problem) and solved by the Potsdam University's [ASP (Answer Set Programming)](http://en.wikipedia.org/wiki/Answer_set_programming) solver: [*clasp*](http://www.cs.uni-potsdam.de/clasp/). The solver is tested with two optimization modes: gap minimization and resource cost minimization. The detailed report of the analysis can be found [**here**](https://www.dropbox.com/s/l5rgp5jtbnww03e/Walling%20in%20at%20StarCraft.pdf).


[Here is a video of ITUBot walling in.](http://www.youtube.com/watch?v=WdhIv_yxIbM)

-------------

The project is coded on Visual Studio 2008 Express.

Related Events/Groups
=======

 - [Student StarCraft AI Tournament](http://www.sscaitournament.com/)
 - [AIIDE StarCraft AI Competition](http://webdocs.cs.ualberta.ca/~cdavid/starcraftaicomp/index.shtml)
 - [BWAPI Facebook Group](https://www.facebook.com/groups/bwapi/)
