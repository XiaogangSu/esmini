#include "scenarioenginedll.h"
#include "ScenarioEngine.hpp"

#ifdef _SCENARIO_VIEWER

#include "viewer.hpp"
#include "RubberbandManipulator.h"

#define VISUALIZE_DRIVER_MODEL_TARGET
#define EGO_ID 0	// need to match appearing order in the OpenSCENARIO file

typedef struct
{
	int id;
	viewer::CarModel *carModel;
	roadmanager::Position pos;
} ScenarioCar;

static std::vector<ScenarioCar> scenarioCar;

static viewer::Viewer *scViewer = 0;

#endif

static ScenarioEngine *scenarioEngine = 0;
static ScenarioGateway *scenarioGateway = 0;
static roadmanager::OpenDrive *roadManager = 0;
double simTime = 0;
double deltaSimTime = 0;  // external - used by Viewer::RubberBandCamera
static char *args[] = { "kalle", "--window", "50", "50", "1000", "500" };

#ifdef _SCENARIO_VIEWER

ScenarioCar *getScenarioCarById(int id)
{
	for (size_t i=0; i<scenarioCar.size(); i++)
	{
		if (scenarioCar[i].id == id)
		{
			return &scenarioCar[i];
		}
	}

	return 0;
}

#endif

static void resetScenario(void )
{
	simTime = 0; // Start time initialized to zero
	deltaSimTime = 0;
	scenarioGateway = 0;

	if (scenarioEngine != 0)
	{
		delete scenarioEngine;
		scenarioEngine = 0;
		printf("Closing scenario engine\n");
	}

#ifdef _SCENARIO_VIEWER
	if (scViewer != 0)
	{
		scenarioCar.clear();
		delete scViewer;
		scViewer = 0;
		printf("Closing viewer\n");
	}
#endif
}

static void copyStateFromScenarioGateway(ScenarioObjectState *state, ObjectStateStruct *gw_state)
{
	state->id = gw_state->id;
	//			strncpy(state->name, gw_state->name, NAME_LEN);
	state->timestamp = gw_state->timeStamp;
	state->x = (float)gw_state->pos.GetX();
	state->y = (float)gw_state->pos.GetY();
	state->z = (float)gw_state->pos.GetZ();
	state->h = (float)gw_state->pos.GetH();
	state->p = (float)gw_state->pos.GetP();
	state->r = (float)gw_state->pos.GetR();
	state->speed = (float)gw_state->speed;
	state->roadId = (int)gw_state->pos.GetTrackId();
	state->laneId = (int)gw_state->pos.GetLaneId();
	state->s = (int)gw_state->pos.GetS();
	state->laneOffset = (int)gw_state->pos.GetOffset();

}

extern "C"
{
#ifdef _SCENARIO_VIEWER
	SE_DLL_API int SE_Init(const char *oscFilename, int useViewer)
#else
	SE_DLL_API int SE_Init(const char *oscFilename)
#endif
	{
		resetScenario();

		// Create scenario engine
		try
		{
			// Create a scenario engine instance
			scenarioEngine = new ScenarioEngine(std::string(oscFilename), simTime);

			// Fetch ScenarioGateway 
			scenarioGateway = scenarioEngine->getScenarioGateway();

			// Fetch ScenarioGateway 
			roadManager = scenarioEngine->getRoadManager();

#ifdef _SCENARIO_VIEWER

			if (useViewer)
			{
				// For some reason can't use args array directly... copy to a true char**
				int argc = sizeof(args) / sizeof(char*);
				char **argv;
				argv = (char**)malloc(argc * sizeof(char*));
				for (int i = 0; i < argc; i++)
				{
					argv[i] = (char*)malloc((strlen(args[i]) + 1) * sizeof(char));
					strcpy(argv[i], args[i]);
				}

				// Initialize the viewer
				scViewer = new viewer::Viewer(roadManager, scenarioEngine->getSceneGraphFilename().c_str(), osg::ArgumentParser(&argc, argv));
			}
#endif
		}
		catch (const std::exception& e)
		{
			LOG(e.what());
			scenarioEngine = 0;
			scenarioGateway = 0;
#ifdef _SCENARIO_VIEWER
			scViewer = 0;
#endif
			return -1;
		}

		// Step scenario engine - zero time - just to reach init state
		// Report all vehicles initially - to communicate initial position for external vehicles as well
		scenarioEngine->step(0.0, true); 

		return 0;
	}

	SE_DLL_API void SE_Close()
	{
		resetScenario();
	}

	SE_DLL_API void SE_Step(float dt)
	{
		if (scenarioEngine != 0)
		{
			// Time operations
			deltaSimTime = dt;
			simTime += dt;

			scenarioEngine->setSimulationTime(simTime);
			scenarioEngine->setTimeStep(dt);

			// ScenarioEngine
			scenarioEngine->step((double)dt);

#ifdef _SCENARIO_VIEWER
			// Update graphics
			if (scViewer)
			{

				// Fetch states of scenario objects
				for (int i = 0; i < scenarioGateway->getNumberOfObjects(); i++)
				{
					ObjectState *o = scenarioGateway->getObjectStatePtrByIdx(i);
					ScenarioCar *sc = getScenarioCarById(o->state_.id);

					// If not available, create it
					if (sc == 0)
					{
						ScenarioCar new_sc;

						LOG("Creating car %d - got state from gateway", o->state_.id);

						new_sc.id = o->state_.id;

						// Choose model from index - wrap to handle more vehicles than models
						int carModelID = i % scViewer->carModels_.size();
						new_sc.carModel = scViewer->AddCar(carModelID);

						// Add it to the list of scenario cars
						scenarioCar.push_back(new_sc);

						sc = &scenarioCar.back();
					}
					sc->pos = o->state_.pos;
				}

				// Visualize scenario cars
				for (size_t i=0; i<scenarioCar.size(); i++)
				{
					ScenarioCar *c = &scenarioCar[i];
					c->carModel->SetPosition(c->pos.GetX(), c->pos.GetY(), c->pos.GetZ());
					c->carModel->SetRotation(c->pos.GetH(), c->pos.GetR(), c->pos.GetP());
				}

				// Update debug visualization items (road positions, steering target and such)
				// Assume first car to be the Ego (Vehicle Under Test)
#ifdef VISUALIZE_DRIVER_MODEL_TARGET
				if (scenarioCar.size() > 0)
				{
					scViewer->UpdateVehicleLineAndPoints(&scenarioCar[0].pos);
					scViewer->UpdateDriverModelPoint(&scenarioCar[0].pos, 25);
				}
#endif
				scViewer->osgViewer_->frame();
			}
#endif
		}
	}

	SE_DLL_API int SE_ReportObjectPos(int id, char *name, float timestamp, float x, float y, float z, float h, float p, float r, float speed)
	{
		if (scenarioGateway != 0)
		{
			scenarioGateway->reportObject(ObjectState(id, std::string(name), timestamp, x, y, z, h, p, r, speed));
		}
		
		return 0;
	}

	SE_DLL_API int SE_ReportObjectRoadPos(int id, char * name, float timestamp, int roadId, int laneId, float laneOffset, float s, float speed)
	{
		if (scenarioGateway != 0)
		{
			scenarioGateway->reportObject(ObjectState(id, std::string(name), timestamp, roadId, laneId, laneOffset, s, speed));
		}
		
		return 0;
	}

	SE_DLL_API int SE_GetNumberOfObjects()
	{
		if (scenarioGateway != 0)
		{
			return scenarioGateway->getNumberOfObjects();
		}
		else
		{
			return 0;
		}
	}

	SE_DLL_API int SE_GetObjectState(int index, ScenarioObjectState *state)
	{
		if (scenarioGateway != 0)
		{
			copyStateFromScenarioGateway(state, &scenarioGateway->getObjectStatePtrByIdx(index)->state_);
		}

		return 0;
	}

	SE_DLL_API int SE_GetObjectStates(int *nObjects, ScenarioObjectState* state)
	{
		int i;

		if (scenarioGateway != 0)
		{
			for (i = 0; i < *nObjects && i < scenarioGateway->getNumberOfObjects(); i++)
			{
				copyStateFromScenarioGateway(state, &scenarioGateway->getObjectStatePtrByIdx(i)->state_);
			}
			*nObjects = i;
		}
		else
		{
			*nObjects = 0;
		}
		return 0;
	}

	static int GetSteeringTarget(int object_id, float lookahead_distance, double *pos_local, double *pos_global, double *angle, double *curvature)
	{
		if (object_id >= scenarioGateway->getNumberOfObjects())
		{
			LOG("Object %d not available, only %d registered", object_id, scenarioGateway->getNumberOfObjects());
			return -1;
		}

		roadmanager::Position *pos = &scenarioGateway->getObjectStatePtrByIdx(object_id)->state_.pos;

		pos->GetSteeringTargetPos(lookahead_distance, pos_local, pos_global, angle, curvature);

		return 0;
	}

	SE_DLL_API int SE_GetSteeringTargetPosGlobal(int object_id, float lookahead_distance, float * target_pos)
	{
		double pos_local[3], pos_global[3], angle, curvature;

		if (GetSteeringTarget(object_id, lookahead_distance, pos_local, pos_global, &angle, &curvature) != 0)
		{
			return -1;
		}

		for (int i = 0; i < 3; i++) target_pos[i] = pos_global[i];

		return 0;
	}

	SE_DLL_API int SE_GetSteeringTargetPosLocal(int object_id, float lookahead_distance, float * target_pos)
	{
		double pos_local[3], pos_global[3], angle, curvature;
		if (GetSteeringTarget(object_id, lookahead_distance, pos_local, pos_global, &angle, &curvature) != 0)
		{
			return -1;
		}

		for (int i = 0; i < 3; i++) target_pos[i] = pos_local[i];

		return 0;
	}

	SE_DLL_API int SE_GetSteeringTargetAngle(int object_id, float lookahead_distance, float * angle_f)
	{
		double pos_local[3], pos_global[3], angle, curvature;
		if (GetSteeringTarget(object_id, lookahead_distance, pos_local, pos_global, &angle, &curvature) != 0)
		{
			return -1;
		}

		*angle_f = angle;

		return 0;
	}

	SE_DLL_API int SE_GetSteeringTargetCurvature(int object_id, float lookahead_distance, float * curvature_f)
	{
		double pos_local[3], pos_global[3], angle, curvature;
		if (GetSteeringTarget(object_id, lookahead_distance, pos_local, pos_global, &angle, &curvature) != 0)
		{
			return -1;
		}

		*curvature_f = curvature;

		return 0;
	}
}
